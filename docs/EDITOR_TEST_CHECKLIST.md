# Editor test checklist (compact showcase)

What to try in IW8 Radiant after the 2026-11-15 work. The interactive copy (OK / Wrong / Skip and a note per item, read
back by Claude) is the artifact page "Compact Showcase Checklist"; this file is the same list for the repository.

Map: `samples/dev/iw8r_compact_showcase.map` (rebuild it: `py -3 -I tools/make_compact_showcase.py`). Nine rows run from
the south wall (row 1) to the north wall (row 9), items west to east; each row has a script_origin `row_<n>_<name>` at
its west end. It builds headless with no errors (`--compile`, map id `mp_iw8r_compact`).

## A. Start-up and side panels (no map open)

- **A1 Start-up**: the window answers at once; Materials and Models show plain swatches for a second or two, then thumbnails.
- **A2 Report Freezes**: Tools > Report Freezes to the Console on for the whole run; note any Console line with source "stall".
- **A3 Side panels**: Materials, Models, Prefabs, Effects, Sounds, Images, IW8 Spawns, Undo History in a narrow column:
  nothing cut off, no sideways scrollbar, options wrap, material tiles fill the width, previews under the lists.

## B. Compact showcase, row by row

- **B1 Row 1 materials**: green wet container metal, brick, cracked concrete, grass, sand, corrugated metal, a chain-link
  fence panel (see-through), the custom orange-and-dark checker, a custom see-through grate; Lit shows their relief.
- **B2 Row 2 shapes**: caulk and clip blocks, a tall ladder slab, two ramps, six steps, an arch patch, a grassy bump, a flat patch.
- **B3 Row 3 models**: crate, barrel (1.5x), cafe chair, short lamp, plant pot, pallet, tyre, each turned 30 degrees more; real models.
- **B4 Row 4 live models**: crate, barrel, chair, the custom checker crate (each with a script_origin), a pallet; still in the editor.
- **B5 Row 5 scripted objects**: wooden double door, traffic cone, radio, tyre, drawn as their stock models.
- **B6 Row 6 prefabs**: barrel group, table lamp, single metal door frame, stock barrel; Ctrl+Shift+E enters and leaves one.
- **B7 Row 7 lights**: Lit Preview and Game Lighting (Ctrl+Alt+L): white, red, blue pools, a warm spot down, a reflection probe.
- **B8 Row 8 effects and sound**: Effect Preview: fire, light smoke, sparks, a heat-haze shimmer at the fourth origin; two speakers.
- **B9 Row 9 gameplay**: allies and axis starts, two TDM and two DM spawns, a trigger box, a thin door brush model, a ladder
  tower, two ledges.
- **B10 Move, rotate, undo**: gizmo move and rotate a row 3 model; Undo History shows the steps (current in bold); Ctrl+Z restores.
- **B11 Build**: F7 finishes with no errors.

## C. Camera, render thread and walking (on the compact showcase)

- **C1 Fly**: right mouse + W A S D Q E (Shift faster): smooth.
- **C2 Fly while busy**: start something heavy (open Hideout, a build) and keep flying: the view keeps moving and, when the
  editor catches up, carries on from there (no jump back, no double turn).
- **C3 Walk**: P; bump into props and scripted objects, climb the row 9 ladder tower, mantle the ledges; Esc leaves; no crash.
- **C4 Old path** (only if C1-C3 felt wrong): start with `IW8R_RENDER_THREAD=0` and compare.

## D. Stock maps (edits show within a second; the rebuild is only for the game)

- **D1 Open Hideout**: the window answers while it loads; props appear when ready.
- **D2 Walk on Hideout**: P; no crash.
- **D3 Sky**: File > Stock Map > Relight Stock Map Sky, another map: the sky changes after a one-time fetch.
- **D4 Sun**: Relight Stock Map Sun, a strong colour, Game Lighting on: sunlit surfaces take it at once.
- **D5 Retexture**: worldspawn `iw8r_stock_retexture` `<wall material>=tw/brick_stone_wall_rough_pale`: those surfaces swap.
- **D6 Remove world geometry**: a brush around a wall as an `iw8r_remove_world` entity: the wall vanishes, the box is an outline.
- **D7 Hide props**: Hide Stock Props In Selection, then Show All Stock Props.
- **D8 Flares, heat haze, lit smoke**: Shipment sun flare (hidden behind walls), Hideout tandoor heat haze, smoke darker in shade.

## Re-check list (after the owner's run, 2026-11-15)

The owner's first run: A1, A2, B3, B11, C1, C2, C4, D3, D8 OK and unchanged; B8, C3, D2, D5 wrong; D6, D7 skipped; the
rest OK with notes that led to changes. The interactive copy is the artifact page "Editor Re-check List"; only what
needs checking again is listed.

### A. Side panels

Start the editor with no map open. These follow your A3 notes.

- **RA1 Tab names readable**: Look at the tab names of the left side bar, the lower middle and the lower right sections (dark theme). Expect: Inactive tab names are light grey and readable; the active tab's name is the brightest.
- **RA2 Models panel layout**: Open Models. Switch the tile size (Small, Medium, Large), search for barrel, select a model. Expect: Models are tiles with a Small / Medium / Large size (kept after a restart), names on two lines (the full name in the tooltip). A compact preview box shows under the tiles only while a model is selected, with Preview in 3D.
- **RA3 Thumbnails cached**: Close the editor, start it again, open Materials, Models and Prefabs. Expect: Materials, Models and Prefabs open already filled and stay filled while you scroll fast (no empty tiles). Images may still need a first-time export.
- **RA4 Effect preview in the side bar**: Open Effects and pick a few: vfx/iw8/veh/scriptables/vfx_smoke_light_ch, vfx/iw8/veh/scriptables/shared/vfx_veh_fire_linger_med, vfx/core/expl/spark_death, then others you know. Make the column narrower and wider. Expect: The preview under the list is square, as wide as the panel, with the whole effect centred in it, drawn with its real textures (no faint orbs): smoke puffs, fire, a spark burst, heat haze bending the checker behind it. An effect never shown before says "Exporting with ACTS..." for a few seconds first. Click pauses.
- **RA5 Sound preview in the side bar**: Open Sounds, pick a sound and press its play button. Expect: The first Play prepares the game's sound files once (about 15 s with progress shown; about 700 MB on C:). Then the sound plays, a looping one loops, Stop stops it. Only the common multiplayer sounds play; an alias without a sound file says so.
- **RA6 Tools in the side bar**: Open the side bar's Tools item (after Undo History); search for leak; open the Tools button of the Materials panel. Expect: A Tools item after Undo History lists the Tools menu's commands in groups, with a search box; the buttons run the same commands. The Materials, Models, Effects and Sounds panels each have a Tools button with their own commands.

### B. Compact showcase, again

Open samples/dev/iw8r_compact_showcase.map (rebuilt with the fixes).

- **RB1 Row 1: materials in their shapes**: Textured view, look along row 1. Expect: Grass, sand, dirt and gravel are low ground pads; walls and the chain-link fence are upright panels; the container metal and the custom checker are boxes.
- **RB2 Row 2: patches above the floor**: Look closely at the flat patch and the grass bump where they meet the floor. Expect: No flicker or clipping with the floor: the flat patch sits 2 units up, the bump rises from just above it.
- **RB3 Row 4: live models move**: View > Behaviour Preview on. Watch row 4 for a few seconds. Expect: The map's own script runs: the crate turns, the barrel bobs up and down, the chair disappears and comes back. Turning the preview off puts them back where the map has them.
- **RB4 Row 5: double door**: Look at the first object of row 5. Expect: Both leaves of the double door, side by side, meeting in the middle.
- **RB5 Row 6: lamp, and leaving a prefab**: Look at the lamp. Select a prefab, Ctrl+Shift+E to enter it, then leave it with the Leave Prefab button in the status bar; enter again and press Esc twice. Expect: The tall floor lamp stands on the floor. The status bar shows "Leave Prefab (back to ...)" while inside; the button and Esc with nothing selected both bring you back.
- **RB6 Row 7: light gizmos**: Select each light of row 7 in turn. Expect: Each light shows its radius as a sphere in the light's colour (dimmed when not selected); the spot shows its inner and outer cone and an aim arrow; the probe shows its box. View > Entity Display > Light Ranges turns them off.
- **RB7 Row 8: effects**: View > Effect Preview on, look along row 8 (the first time, an effect may take a few seconds to be exported). Expect: All four draw: fire, light smoke, sparks replaying every couple of seconds, and the heat-haze shimmer (it may need a few seconds for its export the first time). No grey squares while an effect's textures load. Sound is not played from the speakers in the view; the Sounds panel plays sounds.
- **RB8 Row 9: ladder**: Look at the tower in row 9. Expect: A wooden ladder model on the tower's side, with its climbing volume.
- **RB9 Gizmo modes**: Select a row 3 model. Use the gizmo drop-down (Move, Rotate, Scale, All), then rotate the model and move it again. Expect: The toolbar drop-down (also View > Gizmo Mode) switches the gizmo; All shows arrows, rings and scale boxes together, each part highlights and drags on its own. With Local Space on, the gizmo follows the model's turn and drags move along its own axes. The mode is kept after a restart.

### C. Walking and the view

On the compact showcase unless a step says otherwise.

- **RC1 Walk on the showcase**: Open the map (the camera is above it), press P. Walk the rows, bump into props and scripted objects, climb the ladder (hold W into it), mantle both ledges (hold W and Space into them). Esc leaves. Expect: You start on the ground at the nearest spawn, not in noclip. Props, scripted objects and walls block you; the ladder climbs to the top and you step off onto the tower; both ledges (32 and 56 units) mantle.
- **RC2 Movement like the game**: The game's keys: W A S D, Shift sprint (press again while sprinting for tactical sprint), C crouch (C again stands; C while sprinting slides), Ctrl prone, Space jump. Walk, sprint, slide, jump, go prone, and walk into low rubble and wall caps on Hideout. Expect: Speeds, jump height, slide and the view dropping for crouch and prone feel like the game. Walking into low steep props you stop or slide along them without bobbing up and down. Mouse look while walking is slower than before (the game's own sensitivity).
- **RC3 Lamp glare**: Hideout, Game Lighting and Effect Preview on: walk or fly past the hanging lamps and look at them from different angles. Expect: Each lamp shows a soft round glow and faint rings; no square shapes, nothing spinning as you pass or turn.
- **RC4 No vanishing objects**: Hideout: fly slowly away from props and back, and turn on the spot. Expect: Props switch to simpler versions with distance but stay drawn while you can see them (only specks under a pixel drop out). View > Game LOD Cut-off (off by default) brings back the game's own cut-off, where props past their last distance do vanish as in game.

### D. Stock maps, again

File > Open IW8 Stock Map > mp_hideout, unless a step says otherwise.

- **RD1 Opening frames the play area**: Open Hideout (and another map, e.g. Shipment). Expect: The camera starts over the area with the spawns, close enough to see the buildings, not far away above the whole map.
- **RD2 Walk on Hideout**: Press P and walk around: into walls, up stairs, through doorways. Expect: You walk on the map's floors (no noclip). Walls and stairs behave as solid. The Console logs "Stock world collision: N triangles".
- **RD3 Props collide as in game**: Still walking: walk into bushes, crates, cars and fences. Expect: Props that have collision in game (crates, cars, barrels, fences) block you. Props with no collision in game (most bushes and grass) let you through, as in game. The Console logs "Stock prop collision: ..." when the walk starts.
- **RD4 Stock Map panel**: Open the side bar's Stock Map item. Change the sun colour and direction, another map's sky, add a retexture pair. Expect: A globe icon in the side bar while a stock map is open. Sun colour, intensity and direction, the sky list and retexture pairs change the viewport at once (Game Lighting on for the sun); each change is one undo step; Reset to Stock brings the map's own sun back.
- **RD5 Click a wall to retexture it**: Click a wall (nothing selected under it). Then in the Stock Map panel set To = tw/brick_stone_wall_rough_pale and apply. Expect: The wall's triangles around the click are outlined in amber and the status bar names its material; the panel's From field takes it; the wall turns to the pale stone brick within a second.
- **RD6 Remove world geometry**: Draw a brush around a wall, then give it the entity class iw8r_remove_world. Expect: The wall disappears from the view within a second (the brush is drawn as a volume outline); walking, you pass through where it was.
- **RD7 Hide props**: Click a prop (it is outlined and named in the status bar), hide it; also draw a brush around some props, File > Stock Map > Hide Stock Props In Selection, then Show All Stock Props. Expect: A clicked prop is outlined in amber and the Stock Map panel shows "Picked prop: <model> (row N)" with a Hide button: it vanishes, and Show brings it back. The brush route hides every prop inside the brush; Show All Stock Props brings them all back.
