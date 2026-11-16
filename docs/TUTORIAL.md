# Tutorial: your first map

This tutorial walks through `samples/tutorial/iw8r_tutorial.map`, a small arena that uses the main parts of a
multiplayer map. Open it, look at how each part is made, make some changes, and play it in the game.

## 1. Open the map and look around

1. Choose **File > Open** and open `samples/tutorial/iw8r_tutorial.map`.
2. In the camera, hold the right mouse button and use W, A, S and D to fly; the mouse wheel changes the speed.
   Right-click without moving the mouse to open the camera menu.
3. The 2D view shows the map from above. **Ctrl+Tab** switches to the front and side views, and
   **View > Entity Display > Names** labels every entity.
4. Press **Ctrl+L** to turn on the lit preview. The yellow arrow shows the direction of the sun, which matches the
   sun painted in the map's sky.

## 2. What the map is made of

| Part | Made with | Where to edit it |
|---|---|---|
| Floor, walls, building and ramp | Brushes with stock materials | Brush menu, Surface inspector, Materials panel |
| Sky | A brush with the `sky` tool material | Compile > Sun & Sky |
| Crates, lamp posts and cover walls | Prefabs | Prefabs panel, Edit > Enter Prefab |
| Barrels, pallet and table | Static models | Models panel |
| Light and reflection | `light` and `reflection_probe` entities | Entity inspector, Compile > Capture Reflection Probes |
| Lamp hum and smoke | `sound_emitter` and `fx_origin` entities | Sounds panel, Effects panel |
| Spawns | Team, free-for-all and Domination spawns | Spawns panel |
| Domination flags A, B and C | Domination objectives | Primitives > Game Modes |

## 3. Make some changes

1. Click a cover wall in the camera, press **G**, and drag one of the gizmo arrows to move it. **R** rotates and
   **T** scales.
2. Select a stack of crates and choose **Edit > Enter Prefab**. Move a crate, save with **File > Save**, then choose
   **Edit > Leave Prefab**. Every crate stack in the arena now shows the change.
3. Choose **Primitives > Shape Models > Torus** to add a shape with the current material, and scale it with **T**.
4. Choose **Brush > Auto Caulk** to hide the faces that touch other brushes and are never seen.

## 4. Build and play

1. Press **F7** to build the map. The Compile panel shows each step and lists any warnings, such as a spawn inside a
   wall.
2. Press **F5** to build the map, install it, start the game and load the map.
3. In the game, check that you spawn on the west or east side, that the doorways and the ramp can be walked, that
   the Domination flags are in place, and that the lamp posts light the corners.

## 5. Next steps

- The [user guide](USER_GUIDE.md) describes every part of the editor.
- **Help > First Map Guide** checks off these steps in the editor and offers shortcuts for each of them.
