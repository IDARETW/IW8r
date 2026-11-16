# Plan: our own code instead of ACTS, gsc-tool and the x64-zt fork

Owner, 2026-11-16: "we need to stop relying on ACTs, gsc-tool, and the x64-zt fork, we need to integrate its needed
logic in our own code and conventions".

## What the editor uses them for today (survey, 2026-11-16)

| Tool | Called from | What for |
|---|---|---|
| ACTS (`acts.exe fastfile -r mw19replay -g <Replay exe> --oodle <dll>`) | `acts_runner.*` (queue), `material_cache`, `model_cache`, `world_cache`, `stock_sky`, `techset_export`, `asset_audit`, `sound_preview`, `game_look_assets`, `compile_panel` (build check), `main_window*` (vfx, map_ents, com_map, rawfile, image, material, lightdef) | Reading Replay 1.20 zones: images (+ streamed `.pak` / `.xpak`), materials, techsets, xmodels / xmodelsurfs (GLB), particle systems (vfx), map_ents, gfx_map (+ transient tiles), com_map, lightdefs, rawfiles, soundbanks; whole-zone dumps (`-d`) |
| x64-zt (`zonetool.exe`) | `compile_panel` (build), `main_window_stock_edit` / `main_window_look` (stock zone rebuilds), `main_window` (vision and stock edits through `tools/*.ps1`) | Writing zones: a custom map's assets (gfxworld, clipmap / physics, mapents, comworld, scripts, ...), stock zone edits (editzone), the first-person look zone |
| gsc-tool (`gsc-tool.exe`) | `compile_panel` | Compiling the map's generated GSC to IW8 bytecode |
| Oodle (`oo2core_7_win64.dll`, the game's) | handed to ACTS / x64-zt | Fastfile and package (de)compression. It stays the game's own DLL: we load it ourselves instead of through the tools |

What is ours already: the SAB sound banks and FLAC decoding (`sound_preview.cpp`), DDS decoding (`texture_decode.*`),
the particle simulation (`vfx_sim.*`), map parsing and the brush / model geometry, the GLB reader, the menu packs
(`menu_pack.*`: the side panels no longer need an export for their pictures and facts).

## How ACTS reads Replay zones (checked in the fork, 2026-11-16)

ACTS does not parse Replay zones with its own code: `handler_game_mw19_replay.cpp` maps the game executable into its
process (`hook::module_mapper`) and runs the game's own zone loader (DB_Load and the per-asset load functions), then
serializes the loaded assets with a schema read from the game-test PDB (`mw19_schema.*`, `mw19_replay_bindings.hpp`,
about 2,100 lines of bindings). Image pixels come from the zone or the `.xpak` packages (`mw19_xpak.cpp`), meshes are
written as GLB (`mw19_mesh.cpp`), sounds through `mw19_sound.cpp`. So "our own reader" means bringing that approach
(module mapping, the game's loader, the schema-driven reading) into our code, not writing 117 asset loaders from
scratch.

## Licences

| Tool | Licence | What it means for porting |
|---|---|---|
| ACTS | GPL-3 **or** MIT (`LICENSE.md`) | Port under MIT: keep its copyright and MIT notice in the ported files and in the installer's notices |
| x64-zt | GPL-3 | Ported code makes a distributed editor GPL-3 (source must go with it). A rewrite from the formats (research notes, IDA types) instead of the code avoids that. **Owner's call** |
| gsc-tool | GPL-3 (upstream xensik/gsc-tool; we use a downloaded exe) | Same as x64-zt. Our map scripts use a small, generated subset of GSC: a compiler for that subset written from the bytecode format is small |

## Phases

Each phase lands behind the existing tool path (a setting picks ours or the tool) until ours matches the tool's output
on the test zones; then the tool's path is removed.

1. **Zone reading in-process (replaces ACTS).**
   1. Load the game's Oodle DLL ourselves; read the fastfile header, decompress the blocks (`src/core/fastfile.*`).
   2. Port the Replay module mapping and loader hosting (`src/qtapp/replay_loader.*`, MIT notice), one zone at a time.
   3. Port the schema reader and the asset writers the editor uses, in this order: vfx, map_ents, material, image
      (+ xpak), xmodel / GLB, techset, lightdef, rawfile, com_map, gfx_map (+ transient), soundbank.
   4. Each cache (`material_cache`, `model_cache`, `world_cache`, ...) asks our reader instead of `ActsRunner`; the
      outputs are compared with ACTS's on mp_shipment, mp_hideout and common_mp before the switch.
2. **Script compiling (replaces gsc-tool).** A compiler for the GSC our builds generate (`src/core/gsc_compile.*`):
   the IW8 opcode table, string and function references, the script file layout; compared byte for byte with
   gsc-tool's output on the generated scripts of the test maps.
3. **Zone writing (replaces x64-zt).** The custom map's assets written by our code (`src/core/zone_write/*`): mapents,
   comworld, gfxworld, clipmap / physics, the asset list and the fastfile (Oodle through the game's DLL), from the
   research notes and the IDA types (`docs/ida_types`). Checked by loading the written zone with our reader (phase 1)
   and with the game's loader, then in game by the owner. Stock zone edits (editzone) last.
4. **Remove the tools** from Preferences > Tool Paths, the installer and the docs.

## Order of value

Phase 1 first: every preview, cache, the stock map view and the menus depend on reading (the menu packs already cover
the side panels). Phase 2 is small. Phase 3 is the largest and the riskiest: the owner checks each step in game.
