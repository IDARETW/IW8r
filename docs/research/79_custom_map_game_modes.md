# 79. Why a custom map offers only Team Deathmatch, and the fix

Date: 2026-11-11. Roadmap F.11 ("Only TDM can be picked: other modes say the map does not support this"). This
replaces the conclusion of doc 74, which blamed the old proxy DLL. The proxy is not installed; the cause is in the
recompiled client the owner runs.

Legend: **VERIFIED IN CODE** = read in source (file and line given). **NEEDS A LIVE TEST** = only a run can settle it.
Nothing here was run in the game.

## Cause

VERIFIED IN CODE, client source `E:\DW\IW8\integration\replay\DW_MapAvailability.cpp` (as on disk on 2026-11-11,
before the change below):

- `Com_GameInfo_Init` (replacement of the native at 0x10C78D0) loads the stock map list, then calls
  `AppendCustomMaps()`, then loads the game type list.
- `AppendCustomMaps()` gives every installed package a row of the native map table. The row is a copy of Shipment's
  row with the package's name, title and image, and then two fixed writes:
  `Copy(reinterpret_cast<char *>(row.native + 0x40), 32, "war");` and
  `Copy(row.gameTypes, sizeof(row.gameTypes), "war");`.
  The comment above them says why: "the converted packages carry Team Deathmatch spawns only".
- `row.gameTypes` (1024 characters at offset 208 of the 1240 byte row) is what the lobby reads through
  `UI_GetMapGameTypesForCurrentIndex`. With `"war"` there, every other mode is "not supported by this map".
- `map.json` is read for `"title"` (or `"name"`) and `"ladderRungDistance"` only. No key could change the list.

So the game was right about what it was told. The package had no way to say more.

## The fix

Two parts. Both are written; the client part needs a client build by the owner.

1. **Client** (`DW_MapAvailability.cpp`, applied to the working tree; the exact change is
   `docs/research/79_client/DW_MapAvailability.cpp.diff`, made by `tools/patches/dw_gametypes_client.py`):
   - `CustomMap` gets `char gameTypes[96]`.
   - `ReadGameTypes()` reads `"gameTypes"` from `map.json`: a string of lower-case names (`a-z`, `0-9`, `_`, at most
     11 characters each, the size of the native game type name) with single spaces between them. Anything else gives
     an empty list.
   - The row gets that list, or `"war"` when it is empty. The row's own game type (`native + 0x40`) stays `"war"` when
     the list has it, otherwise the first name. What that field is used for was not determined.
   - A package without the key behaves exactly as before.
   - The file compiles with the client's build flags (clang, `-Wall -Wextra -Werror`). It has not been linked or run:
     the client build is blocked by the client project's own state on 2026-11-11 (see doc 77 addendum).
2. **Editor** (`src/core/map_gametypes.*`, `CompilePanel::writePackageGameTypes`):
   - After every finished build the editor adds `"gameTypes": "<list>"` to the package's `map.json` (the converter
     writes that file from fixed fields, so the editor adds the line afterwards). The build log has a
     `game types:` line.
   - The list is detected from the map. A mode is listed when the map has what stock maps carry for it (doc 32):

     | Name | Mode | The map needs |
     |---|---|---|
     | `war` | Team Deathmatch | 2 `mp_tdm_spawn`, 1 `mp_tdm_spawn_allies_start`, 1 `mp_tdm_spawn_axis_start` |
     | `dm` | Free-for-all | 2 `mp_dm_spawn` |
     | `dom` | Domination | 2 `mp_dom_spawn`, both `mp_dom_spawn_*_start`, flag triggers `flag_primary` with labels `_a`, `_b`, `_c` |
     | `sd` | Search and Destroy | `mp_sd_spawn_attacker`, `mp_sd_spawn_defender`, 2 `trigger_use_touch` named `bombzone`, `sd_bomb`, `sd_bomb_pickup_trig` |
     | `ctf` | Capture the Flag | 2 `mp_ctf_spawn`, both starts, `ctf_flag_*`, `ctf_trig_*`, `ctf_zone_*` for both teams |

     Primitives > Game Modes inserts these sets. When nothing is complete the list is `war` (what the client used
     anyway).
   - worldspawn `iw8r_gametypes` (Compile dock > Map Info... > Game modes) names the list by hand, for modes the editor does
     not detect (for example `conf`, `koth`). The editor does not know what those modes need.

## What is not known

- Whether a listed mode then starts and plays (objects, spawn logic, scripts). NEEDS A LIVE TEST for each mode.
- What `native + 0x40` of the map row is used for.
- Which spawn classes Kill Confirmed, Hardpoint and the other modes read. The stock game type scripts on disk
  (`.gscbin`) do not hold readable class names, so this was not looked up.
- Whether the lobby shows a game type name that the game has not loaded. The editor only writes names from the table
  above unless the mapper types others.

## Live tests for the owner (after a client build with this change)

1. A map with only TDM spawns: the lobby offers Team Deathmatch only, as today. `map.json` has `"gameTypes": "war"`.
2. Add Primitives > Game Modes > Spawn Set: Domination and Domination Flags, build, deploy: the build log says
   `game types: game types detected: war dom`, and the lobby offers Domination for the map. Start it: three flags can
   be captured. If the mode is offered but the match is broken, send what the screen shows; the list is then right
   and the mode objects are the problem.
3. Compile dock > Map Info... > Game modes = `war conf`: the lobby offers Kill Confirmed. Say whether it plays.
