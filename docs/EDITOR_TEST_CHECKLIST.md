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
