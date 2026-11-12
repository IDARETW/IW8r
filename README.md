# IW8 Radiant Suite

A Windows map editor for **Call of Duty: Modern Warfare (2019)**, aimed at giving IW8 mappers the depth of
tooling that [iw3xo-radiant](https://github.com/xoxor4d/iw3xo-radiant) gives CoD4 mappers, built for IW8's own
data, not as an IW3 or IW7 conversion front end.

**Target game build:** MW2019 *Replay* **1.20.4.7623265**, `game_dx12_ship_replay.exe`
MD5 `1c238fe327f2ecc3b0db924c5b425439` (`E:\IW8\Builds\1.20-replay`). See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for why this build and which evidence ties formats and addresses to it.

**Status (2026-11-12):** a complete editor and build tool. It edits maps (brushes, patches, entities, prefabs,
materials, models, lights, effects, sound, scripts without code), builds them into the game's own zones and checks
them with the game's loaders offline, starts them in the game (F5), and edits the stock maps, Warzone included
(entities, spawns, loot, buy stations, added models and brushes, installed as patch files). Many newer features are
built and checked offline but **not yet seen in game**; the feature list says which.

| Document | What it holds |
|---|---|
| [docs/features/FEATURES.md](docs/features/FEATURES.md) | **The full feature list, with screenshots** |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | How to use each feature |
| [docs/TEST_CHECKLIST.md](docs/TEST_CHECKLIST.md) | The in-game checks that only the game can answer |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Every item with its status (done, partial, open) and where its proof is |
| [docs/PROGRESS.md](docs/PROGRESS.md) | Working notes for whoever continues the work |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Target build and evidence, components, IW8 format facts, licensing |
| [docs/research/](docs/research/) | Evidence reports (game formats, converter work, Warzone, stock map editing, collision tiles, ...) |

## What it does (short)

- **Editor**: Qt 6 with dockable views; Direct3D 11 camera with textured, lit (sun shadows), fog, wind and effect
  previews; 2D views; Radiant-style brush, patch, clipper, CSG, vertex, UV and terrain tools; lossless `.map` files.
- **Assets**: browsers for every stock material, model, image, effect and sound of the game; custom materials,
  models (OBJ / glTF), sounds and effects; prefab bundles with parameters.
- **Gameplay**: spawns and every core game mode's objects, Warzone game mode presets, loot points, buy stations,
  vehicles, triggers, doors, lifts and other behaviours without scripting, stock scripted objects (doors, cones, cars).
- **Build**: F7 builds the map into the game's zones (x64-zt converter), with collision in one piece or as the game's
  streamed tiles for big maps, and tests every zone with the game's own loaders (ACTS); F5 plays it.
- **Stock maps**: open any stock map with its world and props, change entities, spawns, loot points, buy stations,
  add models and brushes, hide props, rebuild in seconds, install as patch files and restore.

## Build

Prerequisites (all present on the development machine):

- Windows 10/11 x64, Visual Studio 2022 Build Tools with *Desktop development with C++* (MSVC 14.44 tested)
- [xmake](https://xmake.io) 2.8+ (3.0.9 tested)
- Qt **6.8.3** `msvc2022_64` at `E:\Qt\6.8.3\msvc2022_64`. To install it the same way:
  ```bash
  py -3.11 -m pip install --user aqtinstall
  ```
  ```bash
  py -3.11 -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O E:\Qt
  ```
- Optional, for full IW8 key names: a local gsc-tool checkout (here `E:\mw124\atian-cod-tools\deps\gsc-tool`).

From `E:\IW8-Radiant`:

```bash
xmake f -m release --qt=E:\Qt\6.8.3\msvc2022_64 -y
```
```bash
xmake build -y iw8r_tests
```
```bash
xmake build -y iw8r
```
```bash
xmake build -y IW8Radiant
```

Outputs land in `bin\release\`. The editor build runs `windeployqt`, so `IW8Radiant.exe` starts without Qt on `PATH`.

Generate the IW8 canonical key table once (56,738 ids; the editor works without it, showing numeric ids except the
built-in essentials `classname`, `origin`, `angles`, `model`, `target`, `targetname`, `script_noteworthy`):

```bash
py -3.11 -I tools\gen_iw8_keytable.py E:\mw124\atian-cod-tools\deps\gsc-tool\src\gsc\engine\iw8_token.cpp -o data\generated\iw8_canonical_keys.tsv
```

Regenerate the IW8 sample scene (already committed under `samples\iw8`):

```bash
py -3.11 -I tools\make_iw8_sample_room.py
```

## Run

```bash
bin\release\IW8Radiant.exe samples\iw8\iw8_sample_room.map
```

Open a stock IW8 map's entities (Replay 1.20 `mp_hideout`, ACTS dump):

```bash
bin\release\IW8Radiant.exe E:\mw124-conversion-builds\_ents_stock\hideout\mw19replay\mp_hideout\assets\map_ents\maps\mp\mp_hideout.d3dbsp.0.asset.json
```

Controls (defaults; rebind in *Tools → Hotkeys*): RMB-drag look (+W/A/S/D/Q/E fly), arrows / `,` `.` / D C / A Z
stock camera keys, LMB click select in 3D, Shift+LMB select in 2D, LMB-drag inside the selection to move on the
grid, RMB/MMB pan and wheel zoom in 2D, `~` `1`–`9` grid sizes, `[` `]` grid step, Alt+arrows nudge, Space clone,
Backspace delete, Esc deselect, I invert, N entity inspector, End frame selection, Ctrl+Tab next 2D view.

Build a map (needs the x64-zt `buildextraction` build, branch `iw8radiant/build-extraction`, and ACTS; set the paths in
*Tools → Tool Paths*): open the map, press **F7**, and watch the Compile dock. Zones go to the x64-zt output folder
(default `%LOCALAPPDATA%\IW8 Radiant Suite\...\x64zt_output\<map id>`). Then *Compile → Deploy to Replay Custom Maps*,
start Replay, Local Play, **F6**, and pick the map (or press **F5**, which does all of it); see
[docs/MILESTONES.md](docs/MILESTONES.md) M5.1 for the limits.

The same extraction from the command line:

```bash
bin\release\iw8r.exe --keys data\generated\iw8_canonical_keys.tsv compile samples\iw8\iw8_sample_room.map -o out\m5\sample\extraction --map mp_iw8r_sample
```

CLI examples:

```bash
bin\release\iw8r.exe edit samples\iw8\iw8_sample_room.map -o out\room_edit.map --grid 16 --select-brush 5 --move 30 -9 0 --select-entity mp_tdm_spawn_allies_start --set angles "0 90 0"
```
```bash
bin\release\iw8r.exe diff samples\iw8\iw8_sample_room.map out\room_edit.map
```

## Test

One command builds everything and runs every milestone-1 check (core tests, CLI edit/diff, stock IW8 MapEnts
round trip, end-to-end GUI test on Qt's offscreen platform, Direct3D 11 smoke test with screenshot):

```bash
pwsh -NoProfile -File tools\run_milestone1_checks.ps1
```

Reports go to `out\milestone1-<timestamp>\` (`summary.json`, `core-tests.txt`, `gui-test-report.txt`,
`smoke-test.txt`, `screenshot.png`). The GUI test never shows a window or moves the mouse, so it can run while
you use the editor. Nothing in the test suite starts the game or writes into the Replay install.

The GUI test alone (80 checks with the full build; set `IW8R_FULL_COMPILE=1` to include F7, x64-zt, ACTS and a deploy
into a scratch game folder, about a minute):

```bash
bin\release\IW8Radiant.exe --gui-test samples\iw8\iw8_sample_room.map --out out\gui-test
```

## Layout

```
src/core/       lossless map/entity-string model, geometry, document + undo, IW8 MapEnts reader, JSON reader
src/viewport/   D3D11 scene renderer, render/pick cache, camera math (UI-toolkit independent)
src/qtapp/      Qt 6 + Qt Advanced Docking System shell, GUI test
src/cli/        iw8r command line tool
tests/          core tests (synthetic, IW8 sample room, stock IW8 MapEnts, optional grammar corpus)
tools/          key-table generator, sample generator, milestone check runner
samples/iw8/    IW8 sample room (real Replay 1.20 material/model/class names)
data/           generated IW8 key table (local), stock entity inventory, stock MapEnts dumps
third_party/    Qt Advanced Docking System 4.4.1 (LGPL-2.1, built as a DLL)
reference/      iw3xo-radiant clone (reference only, not built)
```

## Third-party

- Qt 6.8.3 (LGPLv3, dynamically linked, deployed by windeployqt).
- Qt Advanced Docking System 4.4.1 (LGPL-2.1), `third_party/qtads`, built as `qtadvanceddocking.dll`.
- The IW8 key table is generated locally from gsc-tool (GPLv3) and is not distributed with the editor.
