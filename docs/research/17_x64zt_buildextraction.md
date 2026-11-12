# 17. x64-zt: buildextraction (external IW3 extraction to Replay 1.20 map zones)

Scope: the `buildextraction` CLI route and the two-hunk change of doc 15 section 6.4. Date: 2026-11-08.

## Result

- Branch `iw8radiant/build-extraction` from `iw8-conversion` HEAD `4ac87b7e`. Commit `3eaf0c13`. Not pushed.
- The test room (doc 15 section 7, with 4 additions listed below) converted and passed the converter's own structural load check. Output: `E:\IW8-Radiant\out\x64zt-extraction-test\output\mp_iw8r_room\`.
- The zones were written to the output folder only. No zone file in `E:\IW8\Builds\1.20-replay\zone` changed.
- Not verified: the room was not loaded in the game.
- Startup of `mp_iw8r_room` in the game is not tested.

## 1. Diff (commit 3eaf0c13)

| File | Change |
|---|---|
| `src/cli/commands.cpp` | +1: `buildextraction` row in the command table |
| `src/cli/commands.hpp` | +1: `build_extraction` declaration |
| `src/cli/zone_commands.cpp` | +55: `build_extraction` handler and a forward declaration of `convert::iw3_iw8::convert_extraction` |
| `src/convert/iw3_iw8/iw3_fastfile.cpp` | +3 / -5: section 6.4 hunks 1 and 2 |

Total: 4 files, 60 insertions, 5 deletions. The working tree keeps CRLF (`core.autocrlf=true`).

Changes in `iw3_fastfile.cpp` (`PrepareFastfile`):
- Hunk 1 (line 10603): the `.ff` existence and extension check runs only when `options.extraction` is empty.
- Hunk 2 (lines 10818-10820): the throw for a missing `skeleton/complete` is removed. The skeleton unlinker block runs only when `!external`.

Deviation from the user brief: the CLI flag is `--output`, not `-o`. `without_flags()` in `src/cli/commands.cpp` drops every single-dash argument before a handler runs, so `-o <dir>` would leave the folder as a stray positional. Changing that parser was out of scope.

## 2. Build

Build tree: `E:\mw124\x64-zt\build` (existed; reused). Output: `build\bin\x64\Release\zonetool.exe`, 12,700,672 bytes.

```
cd E:\mw124\x64-zt
.\generate.bat --games=iw3,iw8          # 2 s, regenerates build\x64-zt.sln
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" build\x64-zt.sln -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo   # 65 s
```

- `vswhere -latest -requires Microsoft.Component.MSBuild` found the MSBuild above (VS 18 Community).
- `generate.bat --games=iw3,iw8` writes `enabled_games.hpp` with `ZONETOOL_CONVERTERS(x) x(iw3, iw8)`. The `convert-iw3-iw8` lib links into `zonetool.exe`.
- A run with `--games=iw8` alone fails at link time: `zone_commands.cpp` calls `convert_extraction` from `convert-iw3-iw8`.
- Build log: `C:\Users\user\AppData\Local\Temp\claude\40643af5ee92b5b9\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\build1.log`. No compiler errors.

## 3. Test extraction

Folder: `E:\IW8-Radiant\out\x64zt-extraction-test\room_extraction\`. The JSON and `.ents` text were extracted from the doc 15 section 7 code blocks by script, not retyped. The probe `extracted\images\room_probe.dds` is 131,192 bytes (64x64 cube, B8G8R8A8, 7 mips, cube flags `0xFE00`, grey 128,128,128,255).

Files as written by section 7, plus the four additions in section 4:

```
complete, skeleton/complete
extracted/maps/mp/room.d3dbsp.replay-world.json          3185 bytes (+ "sky":"")
extracted/maps/mp/room.d3dbsp.replay-collision.json      1031 bytes (+ triangle_contents keys)
extracted/maps/mp/room.d3dbsp.replay-comworld.json        525 bytes
extracted/maps/mp/room.d3dbsp.ents                        423 bytes
extracted/images/room_probe.dds                       131192 bytes
extracted/materials/room/grey.json                       174 bytes
```

Run command (cwd `E:\IW8-Radiant\out\x64zt-extraction-test`):

```
E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe -game iw8 -path E:/IW8/Builds/1.20-replay buildextraction room_extraction --map mp_iw8r_room --replay E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe --acts E:/mw124/atian-cod-tools/build/bin/Release/acts.exe --output E:/IW8-Radiant/out/x64zt-extraction-test/output
```

Full log: `E:\IW8-Radiant\out\x64zt-extraction-test\run.log` (143 lines). Exit 0, 48 s.

## 4. What failed, and the fixes

Four runs failed before the fifth succeeded. Each fix sits in the extraction folder. No converter code changed beyond the section 6.4 hunks.

1. `cannot enumerate IW3 rawfile zone declarations`. `ReadPreparedRawFiles` (`iw3_fastfile.cpp:1550`) reads `<exportRoot>/zone_source`. `exportRoot` is `replay-world.json` up three levels (`:10646`), which is `extracted/`, not the extraction root. A first try at the extraction root was also wrong. Fix: empty `extracted/zone_source/room.zone`. `ReadDeclaredFx` (`:1493-1497`, called at `:10957`) and `AuditSourceAssetDeclarations` (`:1757-1763`) need the same file.
2. `key 'triangle_contents' not found`. `:11516` reads it with `at()`, but `:7347` treats it as optional. Fix: `"triangle_contents":[],"triangle_contents_schema":"material-partitions-v1"` in the collision JSON. The schema tag is checked at `:7351`.
3. `key 'sky' not found`. `iw3_render_assets.cpp:6186` reads `world.at("sky")`. Fix: `"sky":""` in the world JSON. The empty name takes the dark placeholder cubemap path (`:6187-6192`, written for maps with no sky, "such as mp_gulag").
4. `IW3 impact FX export is missing`. `iw3_fastfile.cpp:9714-9733` needs `impactfx/_default.iw3.json` under `extracted/` or a search path. Fix: copied `_default.iw3.json` from `E:\mw124-conversion-builds\evidence\iw3-audio-exporter\mp_test-alias-1\impactfx\` to `extracted/impactfx/`. It is the IW3 default impact table (schema 1, 12 rows), not map data. Its 105 unconverted effects appear in the warnings below.

Doc 15 section 7 needs these four additions to run. The section was marked "checked against the reader code", but these checks were missed.

## 5. Test output

Published output (`output\mp_iw8r_room\`), with the sizes from the file system:

| File | Bytes |
|---|---|
| `mp_iw8r_room\mp_iw8r_room.ff` | 536,732 |
| `mp_iw8r_room\ww_mp_iw8r_room.ff` | 152,528 |
| `mp_iw8r_room\eng_mp_iw8r_room.ff` | 115,604 |
| `mp_iw8r_room\techsets_mp_iw8r_room.ff` | 14,732 |
| `mp_iw8r_room\srv_mp_iw8r_room.ff` | 196 |
| `mp_iw8r_room\map.json` | 54 |
| `mp_iw8r_room.iw8-assets.tsv` | 89,540 |
| `mp_iw8r_room.iw3-assets.tsv` | 27 |

The map zones total 819,792 bytes. The output folder also holds `.iw3-render-cache` (sun shadow and BC7 atlas caches, about 390 MB with `_stock_cache`). Total output: 572 files, 390,099,830 bytes.

Converter self-check (from the log): `validate: mp_iw8r_room.ff loads with 511 top-level asset(s)`. The other zones load with 0, 9, 2 and 87 assets. Result: `validate: map output 'mp_iw8r_room' passes structural checks`.

ACTS: ten `stock: exported ... with ACTS` lines (`mp_shipment` vfx, physicsfxshape, surfacefx, lightdef, xmodel, xmodelsurfs and image; `ww_common*` xmodel; `ww_mp_shipment`). No ACTS error. I did not run a separate ACTS validation command.

Warnings (6 `[warn]` lines):
- `iw3: 105 IW3 impact effect(s) did not convert`. Their slots keep Replay's stock impacts. Cause: the copied impact table.
- `iw3: source map has no exported light grid`.
- `dumpsrc: no .colmap` and `no .comworld` under `prepared/maps/mp/`. Harmless for this room.
- `assets: no images/compass_map_mp_iw8r_room`. The HUD compass uses the default material.
- `scripts: pass --gsc-tool (or set IW8_GSC_TOOL)`. The startup script is minimap-only. Pass `--gsc-tool` for compiled map scripts; the brief did not include it.

Game folder check: no file in `E:\IW8\Builds\1.20-replay\zone` has a modification time after the run start (epoch 1794155533). Three files at the game root changed during the window: `game_dx12_ship_replay.exe.sqlite-shm`, `.sqlite-wal`, and `replay-dw-client.log`. A Replay process (`game_dx12_ship_replay_loc`, PID 21992) was running, and every path in the zonetool log is under `output\`. I attribute these three files to the running Replay process, but I did not confirm it.

## 6. Editor command

```
zonetool.exe -game iw8 -path "E:/IW8/Builds/1.20-replay" buildextraction <extraction folder> --map <id> --replay "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" [--metadata <json>] [--style iw3|iw8] [--acts <acts.exe>] [--output <folder>]
```

- Needs a build with `--games=iw3,iw8` (or `all`).
- The extraction folder needs the doc 15 section 7 layout, plus `extracted/zone_source/<map>.zone` and `extracted/impactfx/_default.iw3.json` (section 4).
- `--output` defaults to `output`. The output holds `<map>\*.ff` and `<map>.iw8-assets.tsv`.
- `--style iw8` passes `MatchIw8Lighting`.
- Other options pass through unchecked to the converter's option map. Converter keys use underscores: `--gsc_tool <exe>`, `--search_path <dir>`. The CLI usage line lists only `--metadata`, `--style`, `--acts` and `--output`.

## 7. Open items

- The CLI validates only `--map`, `--replay` and `--output`. Other flags are not checked.
- AGENTS.md command table not updated with `buildextraction`.
- The commit trailer follows the brief: `Co-Authored-By: Claude Opus 5.5`. AGENTS.md says not to co-author as the agent. The agent that made the commit is Claude Haiku 5.5. Amend the trailer if the repo rule applies.
