# Tutorial: your first IW8 map

This walks through `samples/tutorial/iw8r_tutorial.map`, a small arena that uses the main parts of a map. Open it,
look at how each part is made, change things, and build it into Replay. (Regenerate it any time with
`py -3 -I tools/make_tutorial.py`.)

## 1. Open and look around

1. File > Open `samples/tutorial/iw8r_tutorial.map`.
2. In the camera: hold the right mouse button and use W A S D to fly; the wheel changes speed. Right-click without moving
   for the camera menu (camera speed, FOV, add shapes, move the selection to a surface).
3. The 2D view (Grid) shows the map from the top; Ctrl+Tab cycles top / front / side. View > Entity Display > Names
   labels every entity.
4. Ctrl+L turns on the lit preview: the yellow arrow points at the sun. It matches where the `sky_mp_shipment` sky
   paints its sun (`sundirection -40 235 0`), so shadows fall away from the bright spot in the sky.

## 2. What the map is made of

| Part | How | Where to edit |
|---|---|---|
| Floor, walls, building, ramp | brushes with stock materials | Brush menu, Surface dock, Materials dock |
| Sky | a brush with the `sky` tool material on top, `skyname` on worldspawn | Compile > Sun & Sky |
| Crates, lamp posts, cover walls | `misc_prefab` entities using `prefabs/*.map` | Prefabs dock; Edit > Enter Prefab (Ctrl+Shift+E) |
| Barrels, pallet, table | `misc_model` (static props) | Models dock (double-click places one) |
| Light, reflection probe | `light`, `reflection_probe` | Entity inspector; Compile > Capture Reflection Probes |
| Hum of the lamp, smoke | `sound_emitter`, `fx_origin` | Sounds dock, Effects dock |
| Spawns | `mp_tdm_spawn*`, `mp_dm_spawn`, `mp_dom_spawn*_start` | IW8 Spawns dock |
| Domination A / B / C | `trigger_radius` with `script_gameobjectname dom` | Primitives > Game Modes |

## 3. Make a change

1. Select a cover wall (click it in the camera) and press G, then drag an arrow of the gizmo: it moves in grid steps.
   R rotates (rings), T scales.
2. Edit > Enter Prefab with a crate stack selected opens `prefabs/crate_stack.map`; move a crate, File > Save, then
   Edit > Leave Prefab (Ctrl+Shift+Q): every crate stack in the arena shows the change.
3. Primitives > Shape Models > Torus adds a torus with the current material; scale it with T.
4. Brush > Auto Caulk sets the faces that touch other brushes to caulk (they are never seen).

## 4. Build and play

1. Compile > Compile Map (F7). The Compile dock shows each step; warnings are worth reading (a spawn inside a wall,
   a texture Replay does not have).
2. Compile > Deploy to Replay Custom Maps, or Compile Map and Deploy (Ctrl+F7).
3. In Replay: Local Play, F6, pick "IW8 Radiant tutorial arena". With Live Link (Direct) you can also press F5 in the
   editor: build, deploy and load in one step.
4. Check: you spawn on the west or east side; the doorways are walkable; the ramp reaches the roof; Domination has
   flags A, B (inside the building) and C; the lamp posts light the corners.

## 5. Next

- docs/USER_GUIDE.md covers every tool; docs/TEST_CHECKLIST.md is the in-game checklist for the dev showcase.
- Tools > Web Server lets you open the same map in a browser (2D and 3D views, entity editing, compile).
