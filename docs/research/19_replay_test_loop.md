# 19. Replay in-game test loop for editor-built maps (mw120rproxy)

Date: 2026-11-08. Scope: what the editor needs to support the owner's test loop for maps it builds: install, log, launch, structural pre-check, process/lock facts, and unbaked-lighting risk.

Read-only research. No game was started, stopped or injected. Nothing was written under `E:\IW8\Builds\1.20-replay`. No repo was edited, built or committed. The only file written is this report. The one scratch script (a read-only re-implementation of the proxy's package check) lives in the session scratchpad at `%USERPROFILE%\AppData\Local\Temp\claude\40643af5ee92b5b9\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\s19\check_pkgs.py`.

Legend: **VERIFIED** = read in source/file, or checked by a read-only command. **INFERRED** = derived from source, not executed in game. **UNKNOWN** = not established.

Shorthands:
- `[MR]` = `E:\mw124\mw120rproxy\` (proxy repo root: `LIGHTING.md`, `STOCK_MAP_RENDERING.md`, `build_deploy_start_mw120r.ps1`).
- `[PX]` = `E:\mw124\mw120rproxy\mw120rproxy\` (proxy source; newest `custom_maps.cpp` copy, 2026-09-17; `developer_ui.cpp` 2026-09-29).
- `[TL]` = `E:\mw124\mw120rproxy\mw120rproxy\tools\`
- `[RPL]` = `E:\IW8\Builds\1.20-replay\` (Replay install)
- Other proxy copies that exist and differ: `E:\mw124\mw120rproxy-upload\mw120rproxy\` (older, `custom_maps.cpp` 820 lines, 2026-09-10, different validator message) and `E:\mw124\mw120rproxy-weapon-publication\mw120rproxy\` (2026-09-17). None of the three contains the "startup inventory" log lines that the live log has (see 1.2).

---

## Summary table

| # | Question | Answer | Evidence |
|---|---|---|---|
| 1 | Where does the proxy log go? | `mw120rproxy.log` next to the proxy DLL (`[RPL]`). Truncated at every launch. Trace: `mw120rproxy.trace.log`. Engine: `mw120rproxy.engine.log`. Exceptions: `mw120rproxy.exceptions.log` plus one `mw120rproxy.crash.YYYYMMDD-HHMMSS-PID.log` per run. No rotation. | `[PX]log.cpp:79-86`; `[PX]diagnostics.cpp:199,207`; `[PX]engine_diagnostics.cpp:239` |
| 2 | Log line format | Source build: `[HH:MM:SS.mmm] [INFO][Maps] text`. The live `mw120rproxy.log` (Oct 11) has NO timestamp and a different content set, so it came from another build. | `[PX]log.cpp:173-177`; `[PX]log.h:11-13`; `[RPL]mw120rproxy.log` lines 1-4 |
| 3 | Scan / validate / select / load messages | `scanned %zu custom map package(s)` at startup; `refreshed ...` on F6/R; `package unavailable id=%s reason=%s` per invalid package; `selected '%s'`; `added normal Local Play map entry id=%s title=%s`; `native ui_mapname readback confirmed '%s'`; `native disk open '%s' -> '%s': opened|FAILED`. Full list in 1.3. | `[PX]custom_maps.cpp:423-425,457,463,482`; `[PX]custom_map_ui.cpp:72-76`; `[PX]developer_ui.cpp:467,471`; `[PX]custom_map_loader.cpp:88-89` |
| 4 | Failure markers | `[WARN][Maps] package unavailable ...`, `[WARN][Maps] native ui_mapname did not retain ...`, `[WARN][Cbuf] command not queued ...`, `[ERR ][Maps] selected package path resolution failed ...`, `[WARN][Core] host is not the supported Replay executable ...`, `[ERR ][Core] hook installation failed ...`. | See 1.3 |
| 5 | Exe to launch | `game_dx12_ship_replay.exe` (base). SHA256 `68fb1cbc...32f0` (matches the proxy's required input hash), MD5 `1c238fe3...5439`. Working dir = `[RPL]`. No launch args are required by the proxy. | VERIFIED: `[PX]replay_compiler_target.h:18-22`; `sha256sum`/`md5sum` of `[RPL]game_dx12_ship_replay.exe` |
| 6 | Offline auth | Set in ini: `offline_auth=1`. Needs `[RPL]mw120rproxy.identity.ini` with a nonzero xuid (present: xuid `13392534174441144127`). No command-line flag. | `[PX]config.h:44`; `[PX]dllmain.cpp:188-192`; `[RPL]mw120rproxy.identity.ini` |
| 7 | Can the proxy auto-load a custom map at startup? | NO dedicated ini key or argument. Indirect only: if `ui_mapname` (token `NSQLTTMRMP`) already holds a valid package id while in the Local Play lobby, `SyncSelection` selects it. Something still has to set that dvar. | `[PX]config.h:9-19` (no such key); `[PX]developer_ui.cpp:454-455`; `[PX]custom_map_ui.cpp:86-96` |
| 8 | What does F6 + Enter run? | Enter calls `SelectPackage()`, which submits the native console text `set NSQLTTMRMP <id>` and `set MOLPOSLOMO war` (tokens, not readable names) into the main command buffer. Then the user must close the browser and press Start Match. | `[PX]developer_ui.cpp:520-526,536-537,176` |
| 9 | Startup console command (`+set`, `+devmap`)? | UNKNOWN. The exe contains `+set` and `+devmap` strings and the engine has `Com_ParseCommandLine`, `Com_Init_GameMode_FromCommandLine`, `Sys_SetCommandLineString`. Syntax is not verified and nothing in the proxy wraps it. | `game_dx12_ship_replay.exe.map:406961,406965,407032`; exe strings (see 2.7) |
| 10 | Structural check (what it rejects) | Package folder name = map id (4-15 chars, `mp_` prefix, `[a-z0-9_]`). Exactly five zones `<id>.ff`, `srv_<id>.ff`, `eng_<id>.ff`, `ww_<id>.ff`, `techsets_<id>.ff`, plus optional `map.json`. No `preview.rgba`, no other files, no subfolders, no links. Zone headers must be `IWffc100`/`IWffa100` with specific fields. `map.json` 2-65536 bytes, strict JSON, string-only metadata, `id` must match folder. No stock `<id>.ff` in game root or `zone\`. | `[PX]custom_maps.cpp:37-44,214-226,228-272,278-397` |
| 11 | Would the packages in `mods\mw120r\maps` pass? | INFERRED (re-implemented, not run in the proxy): none of the 9 passes. 7 fail the file-set rule (6 carry `preview.rgba`; `mp_nuketowwn` has `mp_mp_nuketowwn.ff`). 8 of 9 fail the zone header rule, mostly `stream < xfileSize`. See section 3.3. | `check_pkgs.py` run, read-only |
| 12 | Proxy DLL in the game folder | `XInput9_1_0.dll` (the name the deploy script writes) is NOT present. Two renamed files exist: `XInput9_1_0_.dll` (23.7 MB, Oct 11) and `XInput9_1_0 (2).dll` (157 KB, Oct 16). Neither matches the source Release build size (1.86 MB, Oct 4). Which proxy is live now: UNKNOWN. | `ls` of `[RPL]`; `[PX]xmake-out\x64\Release\XInput9_1_0.dll` size |
| 13 | Process names | `game_dx12_ship_replay.exe` (base); `game_dx12_ship_replay_optimized.exe`; `game_dx12_ship_replay_recompiled.exe` and `_recompiled_shifted.exe` (the name list accepts them only if compiled); `game_dx12_ship_replay_dw.exe` and `game_dx12_ship_replay_local_dw.exe` (present, but NOT in the proxy name list). Detection pattern used by the deploy script: `game_dx12_ship_replay%.exe`. | `[PX]replay_compiler_target.h:12-17`; `[TL]deploy_custom_map.ps1:37`; `tools/live/live.py:36` |
| 14 | Files locked while Replay runs | `mw120rproxy.engine.log` and `mw120rproxy.exceptions.log` (handles kept open, no delete share). `mw120rproxy.trace.log` (kept open by the CRT). `XInput9_1_0.dll` (loaded module). Zone `.ff` files under `mods\mw120r\maps\<id>\` while loaded: UNKNOWN. `mw120rproxy.log` is opened per line and is not held. | `[PX]engine_diagnostics.cpp:239-240`; `[PX]diagnostics.cpp:200-211`; `[PX]log.cpp:197-240` |
| 15 | Unbaked lighting: what goes dark? | Build script silently skips the GPU light grid when no `*.d3dbsp.lightgrid.json` exists, and lightmaps come only from the CoD4 lighting compile. Docs: packages without the grid "can leave these objects nearly black in shadow". Dark or black result for an unbaked map is INFERRED, not observed. | `[TL]build_mp_test.py:460-465`; `[TL]map_lighting.py:65-66`; `[MR]LIGHTING.md:91-92` |
| 16 | Dvars that force fullbright/debug lighting | `r_fullbright` (token `MTROSKKNSL`), `r_lightMap` (`TSNNNOLOQ`), `r_showLightmap` (`MMMLSPLMOL`), `r_lightTweakEnable` (`NPRKOOLRPL`), `r_debugShaderLightmapDensity` (`MMNNKNTOKQ`), `r_lightGridDrawDebugVolumes` (`MPTORROPSS`). All exist as strings in the exe. Effects and defaults: UNKNOWN. The proxy never reads or sets them. | `[PX]dvar_names.h:3507,6711,2115,4533,2144,2792`; `game_dx12_ship_replay.exe.map:798230,798376,798378,798713` |

---

## 1. Logs

### 1.1 Files in the game folder (names, sizes, dates only)

Listed with `ls -la` on `[RPL]`. Writer shown where the source confirms it.

| File | Size | Modified | Writer |
|---|---|---|---|
| `mw120rproxy.log` | 497,150 | 2026-10-11 17:50 | proxy (older build, see 1.2) |
| `mw120rproxy.engine.log` | 550,522 | 2026-10-04 16:52 | proxy `enginediag` |
| `mw120rproxy.trace.log` | 0 | 2026-10-05 | proxy trace (kept open) |
| `mw120rproxy.exceptions.log` | 0 | 2026-10-11 17:47 | proxy `diagnostics` |
| `mw120rproxy.identity.ini` | 63 | 2026-09-07 | proxy `offline_auth` |
| `mw120rproxy.online.ini` | 220 | 2026-10-11 | proxy/online config |
| `mw120rproxy - Copy.log`, `mw120rproxy.engine - Copy.log`, `mw120rproxy.exceptions - Copy.log` | 17,137 / 187,705 / 0 | 2026-09-16 | copies, not current |
| `mw120r-diag.log` | 516,670 | 2026-10-15 | writer not in the proxy source (UNKNOWN) |
| `mw120r-diag.previous.log` | 1,334,878 | 2026-10-15 | as above |
| `mw120r-lightprobe.log` | 11,170,019 | 2026-10-25 | writer not in the proxy source (UNKNOWN) |
| `replay-dw-client.log` | 64,232,885 | 2026-11-08 13:18 | DW client build (`live.py` reads it), not `mw120rproxy` |
| `replay-dw-test.command` | 33 | 2026-11-08 13:11 | DW channel (`tools/live/live.py:33`) |
| `replay-dw-test.enabled` | 0 | 2026-11-07 | DW channel |
| `.proxy\control.ini`, `.proxy\control-result.json` | 80 / 125 | 2026-09-30 / 2026-09-29 | proxy file endpoint (`developer_ui.cpp:209-395`) |

No `mw120rproxy.crash.*.log` exists. No `mw120rproxy.log` from the current source build exists.

### 1.2 Where and how the proxy logs

- Path: next to the loaded proxy DLL, computed from `GetModuleFileNameW` (`[PX]log.cpp:72-80`). Main log `mw120rproxy.log`, trace `mw120rproxy.trace.log`.
- Rotation: none. Both files are truncated when `log120r::Init` runs (`[PX]log.cpp:81-86`, comment "so each launch has a fresh log"). Copy the log before the next launch if it matters.
- Line format: `[HH:MM:SS.mmm] [LEVEL][Category] message`, with `LEVEL` = `INFO`, `WARN`, `ERR ` (`[PX]log.cpp:57-62,177`; macros at `[PX]log.h:11-13`). Each line is appended and the file closed (`log.cpp:185-190`), so it can be read while the game runs.
- Console: when `console=1` the same lines go to stdout (`log.cpp:181-184`).
- Engine messages (`[INFO][EngineNet] t=... ch=...`) go to `mw120rproxy.engine.log` and to the log, with rate-limiting (`log.cpp:124-164`).
- **Version mismatch (VERIFIED):** the live `[RPL]mw120rproxy.log` starts with `[INFO] Client logs | PID 20828 | mw120rproxy.log` and has no timestamps. It contains `startup inventory: <map> present|missing fastfiles` (lines 1043-1130) and `one-time local selector inventory complete: 17/87 maps`. No line in the three proxy source copies produces those strings, and the live log has zero `scanned ...`, `package unavailable`, or `selected '...'` lines. The live log therefore came from a different build. The editor's parser must accept both `[HH:MM:SS.mmm] [INFO][Maps]` and `[INFO][Maps]`.
- The proxy banner in the source (`[PX]dllmain.cpp:155-157`) is `mw120rproxy native-fastfile-only-v1 2026-09-16; target 1.20.4.7623265-replay`. Check for it to confirm which build wrote the log.

### 1.3 Message catalogue (exact formats, source `[PX]`)

Scan and startup:
- `[INFO][Core] mw120rproxy native-fastfile-only-v1 2026-09-16; target 1.20.4.7623265-replay` (`dllmain.cpp:155-157`).
- `[INFO][Core] config: hook_bool=%d hook_variant=%d offline_auth=%d map_monitor=%d map_redirect=%d trigger=%s` (`dllmain.cpp:163-168`).
- `[INFO][Core] developer_ui=%d custom_map_loader=%d` (`dllmain.cpp:171-172`).
- `[INFO][Trigger] game splash bitmap 100 loaded; ...` (success gate, from the live log).
- `[INFO][Core] enabled startup hooks installed; waiting for game registration` (`dllmain.cpp:99`).
- `[INFO][Maps] scanned %zu custom map package(s)` (`custom_maps.cpp:457`, at startup, `dllmain.cpp:220`).
- `[WARN][Maps] package unavailable id=%s reason=%s` (`custom_maps.cpp:423-425`, once per invalid folder per scan). `reason` is one of the strings in section 3.1, or `unknown structural validation failure`.
- `[INFO][Maps] refreshed %zu custom map package(s)` (`custom_maps.cpp:463`, on F6 open, R key, or `select` control action).
- `[INFO][Maps] added normal Local Play map entry id=%s title=%s` and `[INFO][Maps] normal map selector ready: custom=%u total=%d` (`custom_map_ui.cpp:72-76`). Entries are added only for valid packages, and only when Replay builds its map list.
- `[WARN][Maps] native map list has no Shipment template; custom menu entries deferred` (`custom_map_ui.cpp:44`).

Select:
- `[INFO][Maps] selected '%s'` (`custom_maps.cpp:482`, from `Select()`).
- `[INFO][Maps] normal UI selected custom package %s` (`custom_map_ui.cpp:94`, from the lobby's `ui_mapname`).
- `[INFO][Cbuf] queued for local client zero on the game main thread: %s` (`developer_ui.cpp:111`). The payload contains the two `set` lines.
- `[WARN][Cbuf] command not queued: native buffer is unavailable or full` (`developer_ui.cpp:113`).
- `[INFO][Cbuf] translated console dvar names: %s` (`developer_ui.cpp:579`).
- `[INFO][Maps] native ui_mapname readback confirmed '%s'` (`developer_ui.cpp:467`), or `[WARN][Maps] native ui_mapname did not retain '%s'` (`developer_ui.cpp:471`) after 300 frames.

Load (zone reads):
- `[INFO][Maps] selected package disk routing installed at Sys_OpenFileReliable RVA 0x13F4D50; native handle/read/close ownership retained` (`custom_map_loader.cpp:115-117`).
- `[INFO][Maps] initializing native unsigned stored decoder for selected '%s'` (`custom_map_loader.cpp:61`).
- `[INFO][Maps] native disk open '%s' -> '%s': %s flags=0x%X` with `opened` or `FAILED` (`custom_map_loader.cpp:88-89`). This is the line that proves a custom zone was actually served.
- Trace-only (`mw120rproxy.trace.log`): `[PackageRead] '%s' -> '%s' opened=%d flags=0x%X error=%lu` (`custom_map_loader.cpp:85`).

Control and UI:
- `[INFO][Control] request=%u action=%ls accepted=%d` (`developer_ui.cpp:394`).
- `[INFO][UI] native MP overlay reached; tilde/F7 console and F6 custom-map browser available` (`developer_ui.cpp:458-460`).
- `[INFO][UI] Replay native console, dvar-name translation and F6 custom-map browser installed` (`developer_ui.cpp:620-622`).
- `[INFO][UI] console key=%d virtual=%d active=%d` (`developer_ui.cpp:514`).

Failure markers:
- `[WARN][Maps] package unavailable ...` (see above).
- `[ERR ][Maps] selected package path resolution failed; native request retained` (`custom_map_loader.cpp:78`).
- `[WARN][Maps] native ui_mapname did not retain '%s'` (`developer_ui.cpp:471`).
- `[ERR ][UI] developer UI callback failed; native overlay returned normally` (`developer_ui.cpp:479`).
- `[ERR ][UI] package browser input failed: %s` (`developer_ui.cpp:551`).
- `[WARN][Core] host is not the supported Replay executable; XInput forwarding only` (`dllmain.cpp:176`). This means the proxy did NOT hook the game.
- `[ERR ][Core] hook installation failed; inspect preceding error (not reported as installed)` (`dllmain.cpp:101-102`).
- `[ERR ][Trigger] game splash not observed; no game hooks installed (no polling fallback)` (`dllmain.cpp:118-119`).
- `[ERR ][Core] post-splash target bytes do not match 1.20 Replay` (`dllmain.cpp:128`).
- `[ERR ][Core] function address discovery failed; game hooks were not installed` (`dllmain.cpp:183`).
- `[ERR ][Auth] local identity preparation failed or hook_bool=0; check mw120rproxy.identity.ini (must contain a nonzero decimal xuid)` (`dllmain.cpp:190-192`).
- `[ERR ][Core] initialization failed: %s` (`dllmain.cpp:222`).

The browser shows `Status`-style text on screen only, not in the log: `structural checks passed; engine load unverified` for valid packages, or the error string (`custom_maps.cpp:506`; `developer_ui.cpp:439`).

### 1.4 Tail of the live log (short lines only)

The last lines of `[RPL]mw120rproxy.log` (Oct 11) are engine chatter (`[INFO][EngineNet]`, BNET friends RPC errors, stats upload), ending with `Win_ActivateInputMessage: Deactivating low level keyboard hook`. No `[Maps]` line appears after line 1130. The live session therefore never logged a custom-package scan.

---

## 2. Launching

### 2.1 Executable and launchers (VERIFIED)

- Base: `[RPL]game_dx12_ship_replay.exe`. PE timestamp `0x5E9BAF80`, SizeOfImage `0x1324B000` (equal to the proxy's `NativeImageSize`), no `RPLYCMP` identity block (a stock image). SHA256 `68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0` equals the proxy's `InputSha256` (`[PX]replay_compiler_target.h:18-22`).
- Launchers in `[RPL]`:
  - `Run_Optimized_Replay.cmd`: `start "" /d "%~dp0" "%~dp0game_dx12_ship_replay_optimized.exe" %*`. This is the only launcher that forwards arguments (`%*`).
  - `Run_Replay_DW_Local.cmd`: `cd /d "%~dp0"` then starts `game_dx12_ship_replay_local_dw.exe`. No arguments.
  - `Run_Recompiled_Replay.cmd`: starts `game_dx12_ship_replay_recompiled.exe`, which does NOT exist in `[RPL]` (checked). `REPLAY_WORKSTATION_TESTS.txt:6` documents it as the 2026-10-03 workstation exe.
  - `Run_Replay_DW.cmd`: starts `game_dx12_ship_replay_dw.exe` after checking it exists.

### 2.2 Working directory and required args

- Working directory: `[RPL]` (the proxy computes its own folder from its module path, `[PX]config.h:24-31`, `[PX]custom_maps.cpp:454-455`).
- Required args: none found in the proxy. It does not read the command line (no `GetCommandLine` in `[PX]`). Offline auth is controlled by `mw120rproxy.ini` (`offline_auth=1`) plus `mw120rproxy.identity.ini`.

### 2.3 Live proxy ini (game folder) versus source ini

Game folder `[RPL]mw120rproxy.ini` (VERIFIED):
`console=1`, `hook_bool=1`, `offline_auth=1`, `fastfile_diagnostics=0`, `engine_diagnostics=0`, `developer_ui=1`, `custom_map_loader=1`, `hook_variant=0`, `hook_trigger=loadimage`, `custom_map_file_monitor=0`, `custom_map_file_redirect=0`.

Source `[PX]mw120rproxy.ini` differs only in `fastfile_diagnostics=1` and `engine_diagnostics=1`. The deploy script copies the source ini over the game ini unless `-PreserveConfig` (`[MR]build_deploy_start_mw120r.ps1`, `!$PreserveConfig` branch), so a redeploy resets those two keys.

### 2.4 Auto-loading a map at startup

- No ini key (`config.h:9-19`), no command-line parsing and no `[PX]` code path loads a custom package at boot (VERIFIED by searching `[PX]` for `GetCommandLine`, `devmap`, `autoload`, `startup_map`: none).
- Indirect path (VERIFIED): `DrawOverlay` calls `custommapui::SyncSelection(DvarString("NSQLTTMRMP"))` whenever the lobby is active and no selection is pending (`developer_ui.cpp:453-455`). If the dvar equals a known valid package id, `Select()` is called (`custom_map_ui.cpp:86-96`). So any mechanism that sets `ui_mapname` before the lobby shows (command line, console, control file) would select the package. Whether the engine honours that at startup is UNKNOWN.
- Player still has to press Start Match in Local Play. Nothing auto-starts a match.

### 2.5 What F6 and Enter execute (VERIFIED)

1. F6: physical key code `0x9F` (`K_F6`), handled in `HandleKeys` (`developer_ui.cpp:520-526`). It toggles the browser. Opening calls `Refresh()`, which returns early with status `Return to the Local Play lobby before refreshing packages.` unless `CanSelect()` is true (frontend and dvar `LPSPMQSNPQ` = systemlink, `developer_ui.cpp:68-71,116-127`).
2. Up/Down move the selection (`key 132`/`133`). R (`'r'`) refreshes. Backspace (`8`) restores the previous stock map. Esc (`27`) or F6 closes (`developer_ui.cpp:527-543`).
3. Enter (`key 13`) calls `SelectPackage()` (`developer_ui.cpp:536-537`). It submits exactly this text (`developer_ui.cpp:176`):
   ```
   set NSQLTTMRMP <package-id>
   set MOLPOSLOMO war
   ```
   `NSQLTTMRMP` = `ui_mapname`; `MOLPOSLOMO` = `ui_gametype` (`dvar_names.h:5026,2481`). The mode is written as `war`; the manifest word `tdm` is not used (comment at `developer_ui.cpp:175`).
4. Then the proxy waits up to 300 frames for the readback (`developer_ui.cpp:184-185,465-472`).
5. Typing `mw_maps` in the native console also opens the browser (`developer_ui.cpp:571-575`).
6. Tilde (or F7) opens the native console. Readable names are translated to tokens before submission (`developer_ui.cpp:577`, `command_text.cpp`). Example from the source tests: `set ui_mapname mp_test` becomes `set NSQLTTMRMP mp_test` (`[PX]tests/custom_map_tests.cpp:91-96`).

### 2.6 Control file channel (VERIFIED, optional)

`[PX]developer_ui.cpp:206-208,229-230,387-393`: the proxy polls `<game>\.proxy\control.ini` every 250 ms. It runs a request only when `[request] pid` equals the running process id and `id` is new. Actions: `status`, `hotkey` (f6, f7, grave, enter, escape, up, down, backspace), `key`, `player`, `console`, `browser` (open/close), `command` (translated text, needs systemlink), `select` (id, needs frontend and systemlink), `read`. Results go to `.proxy\control-result.json`. The current file shows a past request (`action=command value=com_frontEndAnimation normal`, `id=16911`). This is an owner-run test channel, not a launch method. Editor use is an option, not a requirement.

### 2.7 Startup command line (`+set`, `+devmap`): UNKNOWN

- Strings in `game_dx12_ship_replay.exe` (ASCII search, read-only): `+set` appears 3 times, all in messages such as `Run with +set g_weaponstreaming_verbose 1 for details.`. `+devmap` appears once, in `Redirecting online devmap (+devmap_dedi) to normal devmap (+devmap)`. `+map` appears once (no context found).
- Symbols (`game_dx12_ship_replay.exe.map`): `Sys_SetCommandLineString` (line 406961), `Com_Init_GameMode_FromCommandLine` (406965), `Com_ParseCommandLine` (407032), `Com_Init` (406935), `SvGameModeAppSP::DevmapCmd` (578754).
- Conclusion: the engine probably parses `+`-prefixed startup commands. The syntax, whether readable names work (`+set ui_mapname X` vs token `NSQLTTMRMP`), and whether `+devmap` can load a custom package are all UNKNOWN. The proxy's translator only runs on console text, not on the command line. Test once, owner-run, before relying on it.
- `Run_Optimized_Replay.cmd` forwards `%*`, so args can be tried through that launcher.

---

## 3. Structural check (what the proxy rejects)

### 3.1 Rules, in the order the proxy checks them (`[PX]custom_maps.cpp`)

1. Package folder name = package id. The id is the folder name (`custom_maps.cpp:399-404`). Must satisfy `IsMapId`: 4-15 characters, prefix `mp_`, only `[a-z0-9_]` (`:37-44`). Uppercase is rejected.
2. Not a link: the package directory must not carry `FILE_ATTRIBUTE_REPARSE_POINT` (`:347-350`). The parent `mods\mw120r\maps` may be a link (this install's `maps` is one).
3. No stock collision: `<game>\<id>.ff` and `<game>\zone\<id>.ff` must not exist (`:351-355`).
4. `map.json` (optional; read only if present):
   - not a link (`:285-288`);
   - size 2 to 65,536 bytes (`:220`), else `map.json has invalid JSON`;
   - valid JSON, top-level object, nesting depth at most 16 (`:99-173`);
   - string escapes limited to `\" \\ \/ \b \f \n \r \t`. A `\uXXXX` escape is rejected (`:50-97`);
   - `id`, `title`, `name`, `description` must be strings if present (`:300-305`);
   - if `name` and `title` are both non-empty they must match (`:306-309`);
   - if `id` is present it must equal the folder name (`:312-315`).
   - Quirk: `KeyPosition` returns false on a duplicate key, and `OptionalString` then returns true, silently ignoring that field (`:175-212`). Avoid duplicate keys.
   - Other fields (`compassRotation`, `ladderRungDistance`, `minimapScale`) are not read by the proxy.
5. File set (`:341-385`): the directory must contain exactly `<id>.ff`, `srv_<id>.ff`, `eng_<id>.ff`, `ww_<id>.ff`, `techsets_<id>.ff`, and `map.json` only if present. Any other file (for example `preview.rgba`, `manifest.json`, `*.bin`, `desktop.ini`) fails. Any subfolder fails (`:373-374`, "contains an unreadable entry"). Names are case-sensitive.
6. Zone headers (`:228-272,387-394`), for each of the five `.ff` files:
   - not a link; minimum size `0x8C`;
   - magic `IWffc100` (stored) or `IWffa100` (compressed);
   - header version (`+0x08`) = 11; xfile version (`+0x0C`) = `0xFF7`; encryption (`+0x11`) = 0;
   - `residentPartSize` (`+0x14`) = `fileSize - 0x88`; `xfileSize` (`+0x20`) nonzero;
   - eleven 64-bit block sizes at `+0x30`, each at most the file size; their sum must be at least `xfileSize` (`:257-264,271`);
   - stored: compression byte 0, tag `\x01IWC` at `+0x88`, `residentPartSize == xfileSize + 4`;
   - compressed: compression byte 1, tag `IWffs100` at `+0x88`.
7. Result: `package.valid = true`; status string `structural checks passed; engine load unverified` (`:395,506`).

Not checked by the proxy: file sizes beyond the header, asset contents, map.json fields beyond section 3.1 item 4, `preview.rgba`, bot navigation, collision.

### 3.2 Mismatch with the deploy script (VERIFIED)

`[TL]deploy_custom_map.ps1:82-83` copies `preview.rgba` into the staged package when present. The proxy's file-set rule (`[PX]custom_maps.cpp:382-385`) rejects any extra file, so a package with `preview.rgba` is marked `unavailable` (`:423-425`). One of these must change: the proxy should accept `preview.rgba`, or the deploy script should not copy it. Which one is UNKNOWN; the proxy copy in `mw120rproxy-upload` has the same file-set rule.

### 3.3 Result on the packages currently in `mods\mw120r\maps` (INFERRED)

The folder `mods\mw120r\maps` is a link to `D:\mw124-conversion-builds\game_maps` (VERIFIED with `ls`). Results come from a read-only Python re-implementation of the rules above (`check_pkgs.py`). It is NOT the proxy and was NOT run in Replay. Treat it as a prediction.

| Package | Files | File-set rule | Zone header rule (failing zones) |
|---|---|---|---|
| mp_4doffice | 7 | FAIL: `preview.rgba` | FAIL: `mp_`, `ww_`, `techsets_` (stream < xfileSize) |
| mp_discovery | 7 | FAIL: `preview.rgba` | FAIL: `mp_`, `ww_`, `techsets_` |
| mp_discovery_8 | 7 | FAIL: `preview.rgba` | FAIL: `mp_`, `ww_`, `techsets_` |
| mp_iw7_brkneck | 7 | FAIL: `preview.rgba` | FAIL: all five (tag `\x02IWC`; resident size; block sizes) |
| mp_iw8_sample_r | 6 | PASS | FAIL: all five (tag `\x02IWC`, block sizes) |
| mp_nuketown | 7 | FAIL: `preview.rgba` | FAIL: `mp_`, `ww_`, `techsets_` |
| mp_nuketown_c | 7 | FAIL: `preview.rgba` | FAIL: `mp_`, `ww_`, `techsets_` |
| mp_nuketowwn | 6 | FAIL: `mp_mp_nuketowwn.ff` in place of `mp_nuketowwn.ff` | PASS (all present zones) |
| mp_test | 6 | PASS | FAIL: `mp_`, `ww_`, `techsets_` (stream < xfileSize) |

The `srv_` and `eng_` zones pass the header rule in most packages, except as shown.

Caveat on the stream rule: in every failing `ww_` zone the block sum is about 32 KB short of `xfileSize` (for example 262,569 vs 295,497). The `srv_` zones (172 bytes) pass. Either the rule is stricter than the native loader, or these files were produced by a different converter. The native loader is UNKNOWN. A live scan would settle it. Do not hard-fail the editor on this rule until a live scan confirms it. Use the proxy's own `package unavailable ... reason=` line as the source of truth.

---

## 4. Process and file-lock facts

### 4.1 Executable names

| Name | Present in `[RPL]` | Proxy accepts (`MatchesName`) | Notes |
|---|---|---|---|
| `game_dx12_ship_replay.exe` | yes (Aug 25) | yes (stock image) | Base. The deploy script's `Get-TargetGameProcess` filters on this name only (`build_deploy_start_mw120r.ps1:24`). |
| `game_dx12_ship_replay_optimized.exe` | yes (Oct 4) | yes, only when compiled (`RPLYCMP2` identity) | Launched by `Run_Optimized_Replay.cmd`. |
| `game_dx12_ship_replay_recompiled.exe` | NO | yes if compiled | Launcher exists; exe missing. |
| `game_dx12_ship_replay_local_dw.exe` | yes (Nov 8) | NO (`RPLYCMP2`, but name not in list) | `CheckTarget` would fail, so the proxy would log `host is not the supported Replay executable`. INFERRED from `[PX]replay_compiler_target.h:12-17` and `dllmain.cpp:53-56,175-177`. |
| `game_dx12_ship_replay_dw.exe` | yes (Nov 8) | NO (same as above) | Same. |
| `ModernWarfare.exe` | yes (205 MB) | NO | Different build (timestamp `0x5EB05998`). |

Detection patterns already in the repo:
- `[TL]deploy_custom_map.ps1:37`: `Name LIKE 'game_dx12_ship_replay%.exe'`, filtered to this game root. Use this one.
- `tools/live/live.py:36`: `EXECUTABLES = ('game_dx12_ship_replay_local_dw.exe', 'game_dx12_ship_replay_dw.exe')`.

At research start `tasklist` listed a process `game_dx12_ship_replay_loc` (PID 12932, about 1.4 GB, name truncated by tasklist). It had exited when I checked again, so its loaded modules were not inspected.

### 4.2 Files locked or held while Replay runs

- `[RPL]XInput9_1_0.dll` (the proxy, when loaded): a loaded module cannot be overwritten. The deploy script's `[IO.File]::Replace` would fail and roll back (`[MR]build_deploy_start_mw120r.ps1`). VERIFIED by reading the script; the Windows behaviour is standard.
- `mw120rproxy.engine.log` and `mw120rproxy.exceptions.log`: opened with `FILE_APPEND_DATA`, share read/write only, no delete share (`[PX]engine_diagnostics.cpp:239-240`; `[PX]diagnostics.cpp:200-211`). Readable, not deletable or renamable while running.
- `mw120rproxy.trace.log`: a CRT `FILE*` kept open for the whole run (`[PX]log.cpp:197-240`). Readable, not replaceable.
- `mw120rproxy.log`: opened per line and closed (`log.cpp:185-190`). Not held. But it is truncated at the next launch.
- `mw120rproxy.crash.*.log`: one archive handle per run (`diagnostics.cpp:211`).
- `.proxy\control.ini` and `control-result.json`: read and replaced by the proxy (`developer_ui.cpp:387-393`).
- Zone `.ff` files in `mods\mw120r\maps\<id>\`: the proxy hands the path to the native reader and says the files "remain owned by the native reader" (`custom_maps.cpp:593`). Whether handles stay open after load is UNKNOWN. Treat them as locked while a match runs.
- Crash markers: `dllmain.cpp:238-250` deletes `__game_dx12_ship_replay` and `__ModernWarfare` (16 bytes or less) at attach. `__game_dx12_ship_replay` (Oct 12) and `__replay_13b383fb5e92cc04` (Nov 8, 4 bytes) exist in `[RPL]`. The second one's writer is UNKNOWN.

---

## 5. Unbaked lighting

### 5.1 Build side (VERIFIED)

- Lightmaps come from the CoD4 lighting compile (`compile_lighting`, `[TL]build_mp_test.py:172-203`). They are packed by `map_lighting.pack_coefficients` from `world.get("lightmaps", [])` (`[TL]map_lighting.py:65-66`). If the compile produced none, the list is empty and nothing fails.
- The GPU light grid is generated only when exactly one `*.d3dbsp.lightgrid.json` exists under `lighting\maps\mp`. Otherwise the step is skipped silently (`[TL]build_mp_test.py:460-465`).
- The build report hard-codes `"lighting": "CoD4 baked lightmaps and model shadows"` (`build_mp_test.py:575`) and `"game_tested": False` (`:580`). The report does not record whether the light grid or lightmaps were produced, so the editor must check the output files itself.
- The converter's native bundle path refuses a transient tail without lightgrid and native lightmaps (`E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\iw8\asset_bundle.cpp:1747`, `throw "transient tail requires the preceding lightmap atlas and native GPU light-grid"`). That path is not the `.map` route, but it shows the converter expects both.

### 5.2 Docs (VERIFIED quotes, short)

- `[MR]LIGHTING.md:91-92`: "Packages without that grid can leave these objects nearly black in shadow even when the map surfaces are lit." (Scope: dynamic objects, weapons and arms, without the light grid.)
- `[MR]LIGHTING.md:169-170`: "Graybox imports are deliberately unlit." Rebuild the package to change lighting; a DLL update alone cannot change the native light record or compiled materials.
- `[MR]STOCK_MAP_RENDERING.md:109-111`: copying another map's light grid, visibility or lightmaps would apply data baked for different geometry.
- `E:\mw124d-zt\docs\IW8_CONVERSION.md:143`: "The GPU light grid that lights players and moving models is IW7's own (`<bsp>.gpulightgrid.bin`)."
- `ZONETOOL_TEST_CHECKLIST.md:95`: a dark surface "would mean Replay" (the conversion check's own wording; applies to unlit/black surfaces).

Conclusion for the editor: a map built without the light grid is expected to show dark dynamic objects. A map built without lightmaps is expected to look unlit (INFERRED: not observed in game).

### 5.3 Dvars that change lighting (strings verified; behaviour UNKNOWN)

| Dvar | Token (`dvar_names.h`) | Exe string (`.map` line) | Proxy uses it |
|---|---|---|---|
| `r_fullbright` | `MTROSKKNSL` (3507) | `str_r_fullbright` (798230) | no |
| `r_lightMap` | `TSNNNOLOQ` (6711) | `str_r_lightMap` (798376) | no |
| `r_showLightmap` | `MMMLSPLMOL` (2115) | `str_r_showLightmap` (798713) | no |
| `r_lightTweakEnable` | `NPRKOOLRPL` (4533) | `str_r_lightTweakEnable` (798378) | no |
| `r_debugShaderLightmapDensity` | `MMNNKNTOKQ` (2144) | string present | no |
| `r_lightGridDrawDebugVolumes` | `MPTORROPSS` (2792) | string present | no |

- Set them from the native console with the readable name (`set r_fullbright 1`). The proxy translates it to the token (`command_text.cpp`, `dvar_names.h:3507`).
- Default values, the effect of `r_fullbright 1` on this build, and whether it changes shadowing: UNKNOWN. Test on one map, owner-run.
- `r_lightMap` and `r_showLightmap` are debug views that replace the lit output. They are not a fix for missing data.

---

## 6. UNKNOWN list

- U1. Which proxy DLL is installed now (`XInput9_1_0.dll` is absent; two renamed candidates do not match the source Release size).
- U2. Whether the live proxy log's build (no timestamps, `startup inventory` lines) is the same as any source copy. It is not.
- U3. Whether the native loader accepts the zone headers that the re-implementation rejects (section 3.3). A live scan with the current packages would settle it.
- U4. Whether `preview.rgba` should be accepted or rejected (section 3.2).
- U5. Startup command-line syntax (`+set`, `+devmap`) and whether readable dvar names work on the command line (section 2.7).
- U6. Whether `game_dx12_ship_replay_optimized.exe` passes the proxy's full image check (name accepted; `RPLYCMP2` identity; not run).
- U7. Whether `_dw` and `_local_dw` run with the proxy at all (INFERRED: no, the name is not accepted).
- U8. Whether zone `.ff` handles stay open after a map loads.
- U9. The writer of `__replay_13b383fb5e92cc04`, `mw120r-diag.log`, `mw120r-lightprobe.log`.
- U10. Effects and defaults of the lighting dvars in section 5.3.
- U11. Whether a dark or black result for unbaked maps is what the owner actually sees (not observed).
- U12. Whether `game_dx12_ship_replay.exe` plus the current `XInput9_1_0.dll`-named proxy gives F6 in this install (no live run was done).

---

## What the editor should implement

### A. Log panel

- Read `[RPL]mw120rproxy.log` (the main log). Also offer `mw120rproxy.trace.log` and `mw120rproxy.engine.log` as secondary tabs.
- Read-only. Do not lock: the proxy reopens the main log per line, so tailing is safe. The engine log stays open with read-share, so it is also safe to read.
- Parse both line shapes: `^\[(\d\d:\d\d:\d\d\.\d{3})\] \[(INFO|WARN|ERR )\]\[(\w+)\] (.*)$` and `^\[(INFO|WARN|ERR )\]\[(\w+)\] (.*)$` (the second is the Oct 11 build).
- Filter by category `Maps`, `Cbuf`, `Control`, `Core`, `Trigger`, `UI`. Highlight the messages in section 1.3, and show `package unavailable ... reason=` as a per-map status.
- Show "run state" from the log: the banner (`native-fastfile-only-v1`), `config:` line, `enabled startup hooks installed`, `native MP overlay reached`, `scanned N custom map package(s)`.
- Show a red state for any of: `host is not the supported Replay executable`, `hook installation failed`, `splash not observed`, `native ui_mapname did not retain`, `native disk open ... FAILED`.
- Clear it by copying the file aside before each launch: the proxy truncates it at start.

### B. Launch button

- Exact command (owner-triggered, from the editor UI):
  ```powershell
  Start-Process -FilePath 'E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe' -WorkingDirectory 'E:\IW8\Builds\1.20-replay'
  ```
  No arguments. This matches the base exe the proxy accepts. Do not use `Run_Replay_DW_Local.cmd` or `Run_Replay_DW.cmd` for the custom-map loop (the proxy does not accept those names).
- Before launch, refuse if any `game_dx12_ship_replay*.exe` from `[RPL]` is running (the same filter as `deploy_custom_map.ps1:37`), and refuse if `XInput9_1_0.dll` is missing from `[RPL]`.
- After launch, tell the owner the manual steps and wait for the log markers: splash loaded, `enabled startup hooks installed`, `scanned N custom map package(s)`. Then the owner presses F6, Enter, closes the browser and presses Start Match in Local Play.
- Optional (owner-approved only): `ui_mapname` selection through the control file (`[RPL]\.proxy\control.ini`, `action=select`, `value=<id>`, with `pid` and an incremented `id`), then read `control-result.json`. Only when the game is in the Local Play lobby with systemlink true.
- Optional experiment (owner-run, UNKNOWN result): a `+set`/`+devmap` argument on the launcher. Do not ship until U5 is settled.

### C. Pre-deploy checklist (what the editor checks before copying into `mods\mw120r\maps\<id>\`)

1. No `game_dx12_ship_replay*.exe` running from `[RPL]`.
2. Folder name = map id: 4-15 characters, `mp_` prefix, `[a-z0-9_]` only, lowercase.
3. Exactly the five zones (`<id>.ff`, `srv_<id>.ff`, `eng_<id>.ff`, `ww_<id>.ff`, `techsets_<id>.ff`) and optionally `map.json`. No `preview.rgba` until U4 is settled. No subfolders, no links, no other files.
4. For each zone: header magic `IWffc100` or `IWffa100`, version 11, xfile `0xFF7`, encryption 0, resident size = file size - `0x88`, and the stored/compressed body tag. Report the stream-sum rule as a warning until U3 is settled.
5. `map.json`: UTF-8 JSON object, at most 64 KB, no `\u` escapes, no duplicate keys, metadata values are strings, `id` equals the folder if present, `name` equals `title` if both are present.
6. No stock collision: `[RPL]<id>.ff` and `[RPL]zone\<id>.ff` must not exist.
7. Lighting: warn if the build produced no `*.d3dbsp.lightgrid.json`-derived `*.d3dbsp.gpulightgrid.bin`, or no lightmaps. Warn that dynamic objects may be dark and that `r_fullbright` is the test lever (U10).
8. Build report: do not trust `"game_tested": false` as a pass. Record the proxy's `package unavailable` (or its absence) as the real result.
9. After deploy: copy the old `mw120rproxy.log`, launch, then check for `scanned ...` including the new id, no `package unavailable id=<id>`, and after F6 Enter: `native ui_mapname readback confirmed`, then `native disk open ... opened` for the new zones.
10. Keep the previous package folder. The deploy script already moves it to `.proxy\backups\mw120r-map-<stamp>\` (`deploy_custom_map.ps1` staging/rollback, section 5.2 of doc 03).

Related research: `E:\IW8-Radiant\docs\research\03_build_pipeline_interface.md` (sections 5 and 6), `E:\IW8-Radiant\docs\research\05_runtime_evidence_1_20.md` (sections 4, 5 and 11).
