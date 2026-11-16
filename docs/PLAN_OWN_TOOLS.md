# Plan: ACTS, gsc-tool and x64-zt built from this repository

Owner, 2026-11-16: "we need to stop relying on ACTs, gsc-tool, and the x64-zt fork, we need to integrate its needed
logic in our own code and conventions"; "the point of our project is completely for open source use, making use of
some other tools that i had further developed (ACTs fork, x64-zt fork, gsc-tool and any specific modifications made),
so get the three implemented cleanly into the src and ensure it works, then, we're almost ready to make this public on
github"; "only integrate what's needed".

## Direction

- The project is open source (GPL-3, because x64-zt and gsc-tool are GPL-3; ACTS is GPL-3 or MIT). Each ported tool
  keeps its licence file and copyright lines in its folder, and `THIRD_PARTY.md` lists them.
- Only the code paths the editor calls come in, with the owner's modifications (the forks' own commits). Other games,
  converters, decompilers and UI are left out.
- Each tool lives under `src/tools/<tool>/` with an `UPSTREAM.txt` (where it came from, which commit, what was left
  out), is built by our `xmake.lua` (no premake / CMake, no submodules) and becomes a helper exe beside the editor:
  `iw8r-gsc.exe`, `iw8r-acts.exe`, `iw8r-zt.exe`.
- **Why helper exes and not in-process code.** ACTS and x64-zt map the Replay executable into their own process (ACTS
  runs the game's zone loader; x64-zt's Havok baker maps it at its image base, `/BASE:0x180000000 /DYNAMICBASE:NO`).
  That must not happen inside the editor (address space, crashes, the game's globals). The editor already runs them as
  processes with a queue (`acts_runner.*`, `compile_panel`); the command lines stay the same, so the switch is the
  default tool path.
- Each port is proven by **parity**: the same inputs through the old exe and ours give the same bytes (or the same
  decoded content where the old output has times or paths in it), and the result is checked by a test in the suites.
- Tool Paths keeps an override for each tool (a developer can still point at a fork build), but the defaults are ours.

## What the editor calls (survey, 2026-11-16)

| Tool | Command lines used | Callers |
|---|---|---|
| gsc-tool 1.6.2 (xensik, unmodified) | `-m comp -g iw8 -s pc <stem>.gsc` in the script's work folder (run by x64-zt's `iw3_scripts.cpp`) | `compile_panel` passes `--gsc_tool` to x64-zt |
| ACTS fork (`E:\mw124\atian-cod-tools`, branch `codex/mw2019-complete`, IDARETW) | `fastfile -r mw19replay -g <Replay exe> --oodle <dll>` with `-l`, `-d --noAssetDump`, `--test`, `--serve`, `--xpak`, export types vfx, map_ents, image, material, techset, xmodelsurfs, gfx_map, gfx_map_trzone, lightdef, rawfile, com_map, soundbank; env `ACTS_REPLAY_STRUCTURED`, `ACTS_TRACE` | `acts_runner.*` and the caches (`material_cache`, `model_cache`, `world_cache`, `stock_sky`, `techset_export`, `asset_audit`, `sound_preview`, `game_look_assets`), `main_window*` |
| x64-zt fork (`E:\mw124\x64-zt`, branch `iw8radiant/build-extraction`, 485 local commits) | `-game iw8` with `buildextraction` (`--acts`, `--gsc_tool`, `--iw3_mode_assets off`), `editzone`; the `tools/*` scripts also use `dumpzone`, `buildzone`, `verifyzone` | `compile_panel`, `main_window_stock_edit`, `main_window_look`, `tools/*.ps1` / `*.py` |

## 1. gsc-tool (done 2026-11-16)

- `src/tools/gsc/`: gsc-tool **1.6.2** (the version the editor's `gsc-tool.exe` is, and the one seen working in game;
  x64-zt's submodule is 1.4.10, which writes far-call script names without `.gsc`). Kept: utils, gsc common, lexer,
  parser, preprocessor, compiler, assembler, disassembler (compiled includes), engine `iw8*`. The decompiler and printer
  are trimmed from the context (`tools/patches/gsc_trim_context.py`); `tools/patches/gsc_sync_upstream.py <checkout>`
  re-copies the same file set from a newer upstream.
- `src/tools/gsc/api/iw8r_gsc.*`: `iw8r::gsc::compileIw8Script` (gsc-tool's compile_file and include reader).
  `src/tools/gsc/cli/iw8r_gsc.cpp`: `iw8r-gsc.exe`, gsc-tool's command line for IW8 PC.
- zlib from `third_party/zlib` (zlib licence).
- Parity: all 62 converter scripts saved on the workstation (`x64zt_output/.iw3-scripts`) give the same `.gscbin` bytes
  and the same messages as gsc-tool.exe 1.6.2. Test: `tests/gsc_compiler_tests.cpp` with `tests/data/gsc` (bytes from
  gsc-tool 1.6.2, far calls, errors).
- The editor's default `gsc_tool` path is `iw8r-gsc.exe` beside the editor; the package ships it.

## 2. x64-zt (the IW8 zone writer and the IW3 map converter) (done 2026-11-16)

Done: `src/tools/zonetool` (fork e648a414, 385 files) and `third_party/{zstd,lz4,minizip,nlohmann_json,gsl,glaze,
libtomcrypt,libtommath,directxtex}` copied by `tools/patches/zt_sync_fork.py`; built by `src/tools/zonetool/xmake.lua`
(the fork's settings: C++23, /MT, /O1 with /O2 for IW8 and the converter, no LTO, `/BASE:0x180000000 /DYNAMICBASE:NO`,
without the project-wide /utf-8 and Windows macros) into `bin/<mode>/iw8r-zt.exe`, built with the editor and shipped.
Parity with the fork's zonetool.exe, byte for byte on every file the converter writes:

| Command | Input | Result |
|---|---|---|
| buildextraction | mp_iw8r_fxtest (21 MB extraction) | 33 files identical |
| buildextraction | mp_cinderrail (1.4 GB extraction; extraction unchanged) | 46 files identical, the 394 MB zone included |
| editzone | first-person look (lit filter over 10 shared techset zones) | identical |
| editzone | mp_rust and mp_bm_tut stock edits | identical; a mp_hideout edit fails the same way in both |
| dumpzone, buildzone, verifyzone | mp_hideout with a vision edit (`tools/m2_rebuild_mapents.ps1`) | identical dump and zone, verify passed |

The ACTS exports the converter asks for during a build (`_stock_cache`) differ between any two runs of the same acts.exe;
they are ACTS's output and are checked with ACTS (section 3). Tools: `tools/zt_parity.py`, `tools/zt_editzone_parity.py`.
Defaults switched: Tool Paths `zonetool`, `tools/m2_rebuild_mapents.ps1`, `run_offline_proofs.py`,
`stock_edit_editor_check.py`, `scriptable_offline_test.py`.

Survey (before the port):

Needed: `src/cli`, `src/common`, `src/core`, `src/games/iw8`, `src/games/iw3` (3 files: the converter's source game),
`src/convert/iw3_iw8` (the map pipeline the editor's builds go through). Left out: games h1, h2, iw6, iw7, s1, s2 and
their converters (iw7_iw8 is the IW7 map importer, not used by the editor), shader-tool, minizip.
About 15 MB of source; the largest files are generated tables (`replay_opaque_strings.inc` 3 MB, `structs.hpp` 1.1 MB,
`gsc_tool_iw8_tokens.inc` 0.5 MB).

Libraries: zlib (shared with gsc), zstd, lz4, xxHash, DirectXTex, nlohmann json, GSL, glaze (core io reflection),
libtomcrypt + libtommath, CascLib (core io; left out if only other games use it). Only their source files go into
`third_party/<lib>` with licences; system libraries: bcrypt, d3dcompiler, d3d11, Media Foundation (IW3 MP3 sounds).
Link options of the converter: `/BASE:0x180000000 /DYNAMICBASE:NO` (the Havok baker maps the Replay exe).

Steps:
1. Copy the needed folders into `src/tools/zonetool/` (`tools/patches/zt_sync_fork.py`, a file list like the gsc one);
   `ZONETOOL_GAMES(x) x(iw3) x(iw8)`, `ZONETOOL_CONVERTERS(x) x(iw3, iw8)` as the generated header.
2. xmake targets for the libraries, `zt_common`, `zt_core`, `zt_iw8`, `zt_convert_iw3_iw8` and `iw8r-zt.exe`
   (resources: the converter's `resources.rc`).
3. Parity: build the test maps (sample room, cinderrail, a stock-edit map) with the fork's zonetool.exe and with
   iw8r-zt.exe; compare the zone contents (`dumpzone` / `verifyzone` of both, and the decompressed fastfile blocks).
4. Default `zonetool` path = `iw8r-zt.exe`; `tools/*` scripts take it from the same place.

## 3. ACTS (reading Replay zones)

Needed: the `mw19replay` handler and what it calls. ACTS does not parse Replay zones itself: it maps the game
executable (`hook::module_mapper`), runs the game's zone loader and serializes the loaded assets with a schema read
from the game-test PDB.

- `src/core/acts/tools/fastfile/handlers/handler_game_mw19_replay.cpp` (1,747 lines), `handler_game_mw19.*`,
  `handlers/mw19/*` (unlinkers: localize, luafile, rawfile, scriptfile), `tools/mw19/*` (schema, bindings, payload,
  pools, image, mesh / GLB, sound, xpak; about 7,600 lines).
- The fastfile command and framework it runs in (`tools/fastfile/*`: options, asset pools, dump, handlers registry,
  Oodle decompressor), trimmed to the Replay handler.
- ACTS core and shared pieces they include (`includes.hpp`, logging, utils, hashes, `hook/*`, `deps/oodle`,
  `decryptutils`, `game_data`), and their config files (`config/`, `data/`) that the Replay path reads.
- Libraries: zlib, lz4, nlohmann json (the others of the 35 submodules are for other games and tools).

Steps:
1. Find the closure: build only the handler and the fastfile command, add what the linker asks for, nothing else.
2. `src/tools/acts/` with `LICENSE.md` (GPL-3 or MIT; ported under MIT) and `UPSTREAM.txt`; `iw8r-acts.exe` with the
   `fastfile` command line above.
3. Parity: mp_shipment, mp_hideout and common_mp, every export type the editor uses, ACTS fork output vs ours.
4. Default `acts` path = `iw8r-acts.exe`.

## 4. Switch-over and release

- The editor's defaults point at the helper exes; the package ships them; first-run setup asks only for the game
  (Replay exe and Oodle DLL), which the editor never ships.
- Full build check: a custom map build, a stock zone edit and the exports the side panels and caches use, with only
  the helper exes (no `E:\mw124` paths, `IW8R_TOOL_*` unset).
- Licence: `LICENSE` (GPL-3) at the root, `THIRD_PARTY.md` (each tool and library, licence, source, commit), README
  for GitHub (what it is, building with xmake + Qt, what the user provides: the game).

## Later (not needed for the release)

Moving pieces into the editor in-process (for example the fastfile header and block reader, which needs no game code)
when it helps; replacing the game-loader approach with a reader of our own. Neither blocks the release.
