# 11. ACTS Replay (MW2019) baseline and procedure

Scope: ACTS fork at `E:\mw124\atian-cod-tools`, branch `codex/mw2019-complete`, HEAD `6be9e86d`. The tree is dirty: 5 modified tracked files under `src/core/acts/tools/` and `mw19/`, plus untracked `build-native/` and `build-workstation/`. Use `git status --short --ignore-submodules`. No build was run and `E:\mw124` was not written to. Baseline outputs live under `E:\IW8-Radiant\out\acts-baseline\`.

## 1. Build, regenerate, deploy (read this first)

### 1a. Build acts.exe and acts-common.dll from the current checkout

The existing `build\` and `build-workstation\` directories are stale. `build\CMakeCache.txt` has `CMAKE_HOME_DIRECTORY=E:/Users/Austin/Documents/mw124/atian-cod-tools`, a path that does not exist on this machine. Its `CMAKE_COMMAND` (`C:/Users/user/AppData/Local/Temp/mw120-cmake/...`) is also missing. `build-native\` points at `D:/mw124/atian-cod-tools`. None of them should be reused. Configure a fresh build directory outside the repo so the tracked tree is not dirtied:

```powershell
# Generator in use by all existing caches: Visual Studio 18 2026, x64. Options: NO_QT_BUILD=ON, NO_OPEN_CL_BUILD=ON.
& "C:\Program Files\CMake\bin\cmake.exe" -S "E:/mw124/atian-cod-tools" -B "E:/IW8-Radiant/out/build-iw8r" -G "Visual Studio 18 2026" -A x64 -DNO_QT_BUILD=ON -DNO_OPEN_CL_BUILD=ON

# CLI target produces acts.exe (OUTPUT_NAME "acts", cmake/exe/AtianCodToolsCLI.cmake).
# It depends on AtianCodTools (acts-common.dll), which depends on AtianCodToolsCopyConfig.
& "C:\Program Files\CMake\bin\cmake.exe" --build "E:/IW8-Radiant/out/build-iw8r" --config Release --target AtianCodToolsCLI -- /m:4
```

Notes:
- The configure step rewrites tracked generated headers, `include/acts_api/version_info.h` and `src/core/shared/core/actsinfo.hpp`. WORK_STATUS.md says to restore only those two after a build (`git checkout -- <those two paths>`).
- The earlier logs (`research/mw19/build_*.log`) show the same flags and an MSBuild 18.9.1 invocation. They were run from the old `E:/Users/Austin/...` location, not the current checkout.
- The `deps/antlr4` submodule is reported broken, so a fresh configure may fail there. That was not tested.
- Expected outputs: `E:\IW8-Radiant\out\build-iw8r\bin\Release\acts.exe`, `acts-common.dll`, and `data\mw19\schema.json`.

### 1b. How schema.json reaches bin\Release\data\mw19

`cmake/core/AtianCodTools.cmake` lines 39-43 define target `AtianCodToolsCopyConfig` (ALL). It runs `cmake -E copy_directory config/data -> $<TARGET_FILE_DIR:AtianCodTools>/data`. Line 116 makes `AtianCodTools` depend on it. So every build of `AtianCodToolsCLI` copies the whole `config/data` tree, including `mw19/schema.json`, into `bin\Release\data\`. There is no separate post-build step.

Check after a build:
```powershell
Get-FileHash -Algorithm SHA256 "E:/mw124/atian-cod-tools/config/data/mw19/schema.json","E:/IW8-Radiant/out/build-iw8r/bin/Release/data/mw19/schema.json"
```
Both files should have the same hash. For the existing `build\bin\Release\data\mw19\schema.json`, the hash matches the config copy: `4370aaedc6ba0f3f9f349a5a8263ffbd4d122ba9678bd498bdf75877384eb4b9`.

### 1c. Regenerate config/data/mw19/schema.json

Not run. `scripts/mw19/generate_schema.py` takes no arguments. Its argparse import is unused. Run order:

```powershell
cd E:\mw124\atian-cod-tools
python scripts\mw19\generate_pointer_rules.py      # writes research/mw19/pointer_rules.json (rules + rejected)
python scripts\mw19\generate_schema.py            # writes config/data/mw19/schema.json and docs/mw19/pools.md
                                                  # ALSO rewrites src/core/acts/tools/mw19/mw19_image_formats.hpp (side effect)
```

Inputs, all verified present on disk:
- `research/mw19/game_test_pools.json` (1,958,131 B)
- `research/mw19/native_probe2.json` (17,461 B)
- `research/mw19/alloc_functions.json` (741,846 B)
- `research/mw19/native_root_loaders.json` (229,798 B)
- `research/mw19/pointer_rules.json` (561,798 B, produced by generate_pointer_rules.py from `source_loaders.json`)
- `research/mw19/source_loaders.json` (4,076,189 B)
- Helper modules: `scripts/mw19/replay_layouts.py`, `union_rules.py`, `replay_image.py`

After regeneration, the `E:\mw124` tree will show changes to `config/`, `docs/mw19/pools.md`, `mw19_image_formats.hpp` and `research/mw19/pointer_rules.json`. Rebuild, then re-check the hashes in 1b.

### 1d. Test procedure

Run the tests in the order below. Each one prints to its own output under `build\mw19-tests\` (or the `-o`/`--out` path given). Items marked CLANG need `C:\Program Files\LLVM\bin\clang-cl.exe`. Items marked GAME need the Replay PE, not a running process, except where stated.

| # | Command | Needs | Checks |
|---|---|---|---|
| 1 | `python scripts\mw19\audit_schema.py --output <dir>\schema-coverage.json` | schema only | Static gap inventory for 117 pools. |
| 2 | `python scripts\mw19\test_generator.py` | Python | 5 unittest cases on rule extraction. |
| 3 | `python scripts\mw19\verify_fastfile_bindings.py "E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe"` | GAME (PE on disk) | 111 native bindings; 9 populated layout sites. |
| 4 | `python scripts\mw19\verify_replay.py "<exe>" --out <dir>\verify_replay.json` | GAME (PE on disk) | All 117 pool records. Default `--out` writes into the repo, so always pass `--out`. |
| 5 | `powershell -File scripts\mw19\test_schema.ps1` | CLANG; builds standalone exes | `schema_test.cpp`, image, stream buffer, mesh tests. |
| 6 | `powershell -File scripts\mw19\test_fields.ps1` / `test_process.ps1` / `test_geometry.ps1` / `test_all_pools.ps1` / `test_sound.ps1` / `test_stream.ps1` | CLANG, LLVM; compiles a synthetic `game_dx12_ship_replay.exe` and starts it; uses synthetic PIDs only | Process reader, geometry, sound, stream. Writes reports only. |
| 7 | `powershell -File scripts\mw19\test_linker.ps1` | CLANG, Python | Linker fixtures (8-type linking limit). |
| 8 | `powershell -File scripts\mw19\test_xpak.ps1` | CLANG; expects `build\bin\Release\lz4.lib` (not verified present) | XPak reader. |
| 9 | `python scripts\mw19\test_fastfile.py "<exe>" --out <new empty dir>` | GAME PE; `build\bin\Release\acts.exe` | Synthetic fastfile regression for all 111 loaders. |
| 10 | `python scripts\mw19\test_fastfile_geometry.py "<exe>" --out <new empty dir>` | GAME PE | Synthetic GLB path. |
| 11 | `python scripts\mw19\test_stock_worlds.py --game <exe> --zone <ff> --oodle <dll> --out <new dir>` | GAME PE, zone, oodle | Bounded stock-zone check. |
| 12 | Stock-zone test mode (section 2) | GAME PE, zone files, oodle | Four stock zones, test mode. |

`scripts\mw19\test_gsc_preview.py` covers IW8 GSC decompilation, not Replay, and was not run. The `test/` folder has no MW19 content, only other tools' fixtures.

Process-based tests (item 6) use a child process spawned by the script. A game process already running on the machine is not needed and is not touched by them.

## 2. Baseline: stock zones, acts.exe test mode

Binary: `E:\mw124\atian-cod-tools\build\bin\Release\acts.exe`, SHA256 `c6bb29ec67fb8648476b75783b28c3a8acbfbe24a6238b2de65aaeec4d58df6d`, dated 2026-09-17. This hash does not appear in WORK_STATUS.md, so provenance against HEAD `6be9e86d` is unverified.

Command (per zone):
```
acts.exe --noUpdater fastfile -r mw19replay -g E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe --oodle E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll --test -o E:/IW8-Radiant/out/acts-baseline/<zone> E:/IW8/Builds/1.20-replay/zone/<zone>.ff
```
Working directory and output: `E:\IW8-Radiant\out\acts-baseline\<zone>\`. Runner script: `C:\Users\user\AppData\Local\Temp\claude\40643af5ee92b5b9\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\run_baseline.sh`. Per-zone timeout 1500 s (none reached).

No `--xpak` was supplied, so streamed XModel and SoundBank payloads are absent by design. That explains every `package_key_missing` result below.

### Results

| Zone | Exit code (as recorded) | Wall time (s) | Start-end | Loaded | Tested | Failed | Unavailable | complete | success |
|---|---:|---:|---|---:|---:|---:|---:|---|---|
| mp_hideout | 127 | 15 | 10:29:08-10:29:23 | 18,642 | 50 | 0 | 1 | true | false |
| mp_shipment | 127 | 11 | 10:29:23-10:29:34 | 10,478 | 51 | 0 | 1 | true | false |
| mp_backlot2 | 127 | 15 | 10:29:34-10:29:49 | 12,784 | 49 | 0 | 1 | true | false |
| common_mp | 127 | 7 | 10:29:49-10:29:56 | 10,479 | 31 | 2 | 2 | true | false |
| **Total** | | **48** | | | | | | | |

Exit code 127 is the value the runner recorded. The stdout logs show normal completion and then `Parsed 1 (0x1) file(s) with 1 (0x1) error(s)`. I did not determine why the process returns 127. Treat it as an anomaly to investigate, not as a launcher failure.

Stdout line for each zone:
- mp_hideout: `Replay: 18642 assets loaded, 50 tested, 0 failed, 1 unavailable`
- mp_shipment: `Replay: 10478 assets loaded, 51 tested, 0 failed, 1 unavailable`
- mp_backlot2: `Replay: 12784 assets loaded, 49 tested, 0 failed, 1 unavailable`
- common_mp: `Replay: 10479 assets loaded, 31 tested, 2 failed, 2 unavailable`

Manifest fields (`mw19replay\<zone>\manifest.json`): `complete=true`, `success=false`, `test_only=true`, `error="Replay asset export has failures or unavailable payloads; see manifest.json"`. The `success=false` result is inferred from that error and the nonzero unavailable count. The ACTS source was not read to confirm the rule.

Per-zone non-ok records from `assets.jsonl`:

| Zone | Type | Status | Name | Reason |
|---|---|---|---|---|
| mp_hideout | xmodelsurfs | unavailable | accessory_accessory_scale_lab_lod010v41 | package_key_missing (21,488 B) |
| mp_shipment | xmodelsurfs | unavailable | barrier_chain_link_fence_32_01_rusty_lod010v811 | package_key_missing (85,168 B) |
| mp_backlot2 | xmodelsurfs | unavailable | accessory_plant_pot_fake_2_lod010v811 | package_key_missing (101,600 B) |
| common_mp | xmodelsurfs | unavailable | cp_rave_crossbow_arrow_lod010-ts1-v41 | package_key_missing (25,968 B) |
| common_mp | soundbanktransient | unavailable | donetsk_airfield.all | package_key_missing (4,096 B) |
| common_mp | streamkey | **failed** | common_mp_all_loaded | Invalid payload count |
| common_mp | soundbank | **failed** | common_mp.all | Invalid payload count |

Every zone reports zero `read_errors`, `unresolved_pointers` and `unresolved_unions` across all records. The `loaded_by_type` maps are in each manifest.

### 2a. common_mp: the two failures

Both failures are `Invalid payload count`, on `streamkey common_mp_all_loaded` and `soundbank common_mp.all`. Both are in the sound/stream-key family, and common_mp is the largest zone (746 MB). The cause was not investigated. The unavailable soundbanktransient and xmodelsurfs rows are missing XPak payloads, which the command shape does not supply.

## 3. Schema-only checks (no build, no game process)

Output directory: `E:\IW8-Radiant\out\acts-baseline\`.

- `audit_schema.py`: `pool_count=117`, `unique_potential_gaps=1`, `pools_with_potential_gaps=1`. The single gap is pool 57, `ddl`, member `DDLMember.serializedByte` (`unsigned __int8`), reason `pointer_extent_required`. Report: `schema-coverage.json`.
- `test_generator.py`: 5 tests, OK.
- `verify_fastfile_bindings.py` (on the game PE): success, 111 bindings, 9 populated layout sites.
- `verify_replay.py` (on the game PE, `--out` redirected): success, 117 pools verified, no errors.

The game PE SHA256 is `68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0`, which matches WORK_STATUS.md.

## 4. Environment notes

- A process `game_dx12_ship_replay_loc` (PID 21992) was running during the baseline. It is not the `-g` executable, and the test mode does not attach to processes. It was not touched.
- `E:` had 11 GB free (98% used) before the run. The zone tests write only manifest and journal files.
- Before-run status snapshot: `E:\IW8-Radiant\out\acts-baseline\_mw124_status_before.txt`. Other agents changed the tree after that snapshot: `config/data/mw19/schema.json` is now modified, and `scripts/mw19/`, `docs/mw19/fastfile.md` and `.gitignore` show changes. My runs wrote only under `out\acts-baseline` and `docs\research`.
- Schema caveat: when I checked the hashes at the start of the session, `build\bin\Release\data\mw19\schema.json` and the config copy matched `4370aaed…`. Later, both hash to `41288fd7…`. I did not verify which copy the 10:29 run loaded. Before using this baseline as a regression reference, rerun section 2 against a freshly configured build with the current schema hash recorded.
