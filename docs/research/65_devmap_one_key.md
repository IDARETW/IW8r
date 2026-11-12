# 65. One-key "devmap" (F5): compile, deploy and play the map in Replay 1.20

Date: 2026-11-10. Scope: find a command sequence that really starts a deployed custom map in Replay 1.20 multiplayer,
from the menu and from a running match, plus launch arguments, and implement F5 on it.

Legend: **VERIFIED** = read in the binary / source / log with the reference given. **STATIC** = derived from code, not
run in game. **UNVERIFIED** = needs the owner's in-game test (section 6).

Evidence sources (all read-only; nothing was written under `E:\IW8\Builds\1.20-replay`, no game was started or attached):
- `[DB]` `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe.sqlite` (Diaphora / IDA export of the stock exe: function
  names, pseudocode, disassembly), opened with `?immutable=1`. The IDA MCP server was not reachable (no IDA instance).
- `[EXE]` the stock exe and `game_dx12_ship_replay_local_dw.exe` / `_dw.exe` (PE bytes, `.rplmap`).
- `[DW]` `E:\DW\IW8\integration\replay\*.cpp` (the DW client source: `DW_MapAvailability.cpp`, `DW_MapRefresh.cpp`,
  `DW_LocalPlatform.cpp`, `DW_LobbyControls.cpp`).
- `[LOG]` `E:\IW8\Builds\1.20-replay\replay-dw-client.log` (the owner's DW client log).

## 1. Why the old F5 never worked

F5 sent `map <id>` (Preferences `live/loadMapCommand`, default `map %1`). **Replay 1.20 MP has no `map` or `devmap`
command** (VERIFIED, `[EXE]` + `[DB]`):

- `map`, `spmap`, `spdevmap`, `map_restart`, `fast_restart`, `loadgame`, `nextlevel` are one table at `0x145A05960`
  (name / callback pairs, all `SV_CmdSP_*`), registered only by `SV_CmdsSP_AddOperatorCommands` 0x141B520A0, whose one
  caller is `ComGameModeAppSP::InitializeGameMode`. So they exist in SP only.
- The MP server command table `0x1445C00D0` (registered by `SV_CmdsMP_RegisterCommandList` from
  `ComGameModeAppMP::InitializeGameMode`) holds exactly `hostmigration_start`, `hostmigration_start_backout`,
  `sv_recordEventFF`, `sv_systemlinkStartMatch`.
- The MP devmap code (`SV_DevMap_f` 0x14136BBB0, `SV_DevMapDedi_f`) has **no reference** in the image: no pointer to
  it in any data section and no rip-relative reference in code (byte scan of every executable section). The string
  `devmap` is referenced once, by `Com_CompareStartupCommands` (sorts `+devmap` last on the command line).
- The DW client adds `map_restart`, `fast_restart`, `map_refresh`, `map_reload` for MP hosts (`[DW]`
  `DW_MapRefresh.cpp:652-655`), still no `map` / `devmap`.

## 2. What starts a map in MP: the lobby start

`PartyHost_StartMatch_Internal` 0x14119E3D0 (`[DB]`) is the native match start. It takes the map from
`Party_GetMapName` 0x14118C660, which returns dvar `NSQLTTMRMP` (`ui_mapname`, 0x14D386740 +0x28), and the mode from
`ui_gametype` (`MOLPOSLOMO`), then (listen server) `Com_GameStart_BeginServer(map, gametype)`. Two registered commands
reach it through `PartyHost_RequestStartMatch` 0x14119CFA0 (checks: party running, in a party, we are host, game lobby
or system link lobby):

| Command | Callback | Gate | Use |
|---|---|---|---|
| `xpartygo` | `CL_MainMP_PartyGo_f` 0x1415E4470 (table 0x14461F9F0) | `xblive_privatematch` (`LSTLQTSSRM`) true, valid session | Private match lobby we host |
| `sv_systemlinkStartMatch` | `SV_SystemLinkStartMatch_f` 0x14136B6F0 | `systemlink_host` (`LLPNKKORPT`) true | Local Play lobby we host |

Getting a lobby without menus: `xstartprivatematch` (`CL_MainMP_StartPrivateMatchHost_f` 0x1415E4910: stops the party,
`Party_Awake`, hosts) after `set LSTLQTSSRM 1`. Leaving a running match back to the lobby: `xpartybackout`
(`CL_MainMP_PrivatePartyBackout_f` 0x1415E4370: host in game sends `partybackout` to its server, which backs every
client out; the party stays). All of these strings exist once in the stock and the DW exes (VERIFIED byte search).

Token names come from the exe's own table (`dvarHashToText_s` 0x140C20C30): `NSQLTTMRMP` ui_mapname, `MOLPOSLOMO`
ui_gametype, `LPSPMQSNPQ` systemlink, `LLPNKKORPT` systemlink_host, `LSTLQTSSRM` xblive_privatematch, `OKPMLLKRP`
ui_saved_mapname, `OOTQKOTRM` party_maxplayers.

Custom zones: the DW client routes any open of `<id>.ff`, `srv_/eng_/ww_/techsets_<id>.ff` to
`mods\mw120r\maps\<id>\` (`[DW]` `DW_MapAvailability.cpp` `Sys_OpenFileReliable`, `CustomZonePath`), for packages
found by `ScanCustomMaps` (run by `Com_GameInfo_Init`; the client logs the list as `DW_CUSTOM_MAPS installed: ...`).
So setting ui_mapname to the package id and starting the lobby loads the package. The stock exe without a proxy has no
such routing: in this install `XInput9_1_0.dll` is renamed `.diag-disabled`, so **the stock `game_dx12_ship_replay.exe`
cannot load custom maps at all right now**; the working client is `game_dx12_ship_replay_local_dw.exe`
(`[LOG]`: `DW_CUSTOM_MAPS installed: ... mp_iw8r_dev_sho ...` and `DW_ZONE_REQUEST ... mp_iw8r_dev_sho`).

## 3. Rebuilding the map that is running

- Native `map_restart` / `fast_restart` keep the level zones (`[DW]` `DW_MapRefresh.cpp` header: feature 194 in
  `DB_PrepareMapAssetsForSpawn`); the log says so: `map_restart: native full restart (level zones stay loaded; package
  changes need a map change or map_refresh)`.
- DW `map_reload` unloads every level zone and loads them again from disk. **Verified in the owner's log**: repeated
  `map_reload: mp_iw7_brkneck: level zones unloaded; loading from disk.` then `... level zones reloaded from disk;
  server running.` (`[LOG]` lines ~527465-527486). It refuses a package the client did not scan, a second reload in
  flight, and any reload after a `map_refresh` in the session.
- Files: native zone opens are `GENERIC_READ`, share read only (`Sys_OpenFileReliable`). The editor's deploy already
  swaps files one by one and rolls back when one is open. Whether a loaded level keeps its `.ff` handles open is still
  UNVERIFIED; the deploy before `map_reload` either succeeds (handles closed after load) or fails cleanly, and F5 then
  falls back to `xpartybackout` (zones unload with the level), deploys from the lobby and starts again.
- A package id that is new since the game started: `ScanCustomMaps` runs in `Com_GameInfo_Init`; the log shows a
  `DW_CUSTOM_MAPS installed:` line after each client init. Whether the match start's `Com_Restart` rescans is
  UNVERIFIED; F5 says so in its status when the id is not in the last list.

## 4. Launch arguments

- `Com_ParseCommandLine` 0x1412B03B0 splits the command line at `+` and newline (bit mask 0x80000000401: NUL, LF,
  `+`), at most 128 commands. `Com_Init_Try_Block_Function` sorts them (`Com_SortStartupCommands`: devmap last) and
  prints `Boot arguments: '+...'`. `Com_ExecStartupConfigs` 0x1412AA1B0 (end of `Com_Init`) runs each one through the
  command parser (split at `;` outside quotes). So `+set <dvar> <value>` works at boot (STATIC). `+devmap` / `+map` do
  nothing in MP (section 1).
- `Com_Init_GameMode_FromCommandLine` 0x1412AF5D0 reads startup commands `coreMode`, `mpMode`, `aliensMode`, `cpMode`,
  `spMode`, `noMode` with an int argument and sets the desired game mode (mpMode 1 -> mode 2, MP). **F5 launches with
  `+mpMode 1`** (setting `devmap/launchArgs`) to skip the mode-select screen (STATIC, UNVERIFIED in game).
- No launcher reads arguments for a map: `Host-Local-Server.ps1` starts `game_dx12_ship_replay_local_dw.exe` with
  none; `Run_Optimized_Replay.cmd` forwards `%*`; the DW ini has no map key (`DW_LocalConfig.cpp`). A start command on
  the command line runs before the front end and the party exist, so the match itself is started by the editor after
  boot (section 5).
- DW mode select: `dw_localTestMenuMP` (`[DW]` `DW_LocalPlatform.cpp` TestMenuMP_f) selects Multiplayer from mode 0
  when `replay-dw-test.enabled` existed at launch (it does in this install). F5 sends it when a DW client sits in
  mode 0 (a launcher script cannot pass `+mpMode 1`).

## 5. What F5 does now (`src/core/devmap_plan.*`, `src/qtapp/devmap_launch.*`)

F5 = Build, Deploy and Play in Game (devmap). F5 stays: it is the existing binding (toolbar "Play", web hook
`buildDeployLoad`) and the usual "run" key. After a successful F7 compile the `DevmapRunner` polls the game every
500 ms through the direct link (`live_direct`: memory reads + the native command buffer) and asks
`devmap::decide()` for the next step:

| Game state (read from memory) | F5 does |
|---|---|
| Not running | deploy, start the launch exe (`.exe` with `+mpMode 1`; `.cmd`/`.bat` via `cmd /c`, `.ps1` via PowerShell, no args), wait for the MP menu |
| Mode select (mode 0), DW client | `dw_localTestMenuMP` after 15 s (max 3 tries) |
| MP menu, no party | deploy; after 5 s stable: `set LSTLQTSSRM 1`, `xstartprivatematch`; wait for our lobby (30 s) |
| Lobby we host | deploy; `set NSQLTTMRMP <id>`, `set MOLPOSLOMO war`, `xpartygo` (`sv_systemlinkStartMatch` in a Local Play lobby we host) |
| Match we host, same map, DW client listed the package | deploy, `map_reload`, wait for the DW report |
| Match we host, other map (or reload refused / files in use) | `xpartybackout`, wait for the lobby (60 s), deploy, start as above |
| Someone else's lobby / server, SP / CP | deploy, stop with the reason |
| More than one exe in the game folder | stop |

Success = the local player exists and `ui_mapname` is the map id. Each step and failure goes to the status bar and the
compile log (`F5: ...`). Settings: `devmap/launchArgs` (`+mpMode 1`), `devmap/gametype` (`war`, the only mode the
proxy and DW use for packages), `devmap/reload` (true). Ctrl+F8 (Load Map in Game) now sends the lobby start too when
`live/loadMapCommand` is not set, and refuses outside a lobby we host.

State reads (native RVA; the dvar value at +0x28): game mode byte 0x0C5244E0 (`Com_GameMode_GetActiveGameMode`),
front end int 0x0C4EBEB0 (>= 5, `Com_FrontEndScene_IsActive`), `g_partyData_inParty` 0x0C7E23CC,
`g_partyData_areWeHost` 0x0C7E23C8, local server dvar 0x0D385FB0 (`Com_IsAnyLocalServerRunning`), ui_mapname
0x0D386740, ui_gametype 0x0D386738, systemlink 0x0D386788, systemlink_host 0x0D386790, xblive_privatematch 0x0D386820,
player = g_entities -> client (existing). VERIFIED against both DW exes: the code of `Party_GetMapName`,
`Com_GameMode_GetActiveGameMode`, `Com_FrontEndScene_IsActive`, `Com_IsAnyLocalServerRunning`,
`CL_MainMP_PrivatePartyBackout_f` and `Live_IsInSystemlinkLobby`, mapped through each exe's `.rplmap`, reads exactly
the mapped address of each global (script check, 7 of 7 match); `tests/live_direct_tests.cpp` checks every RVA lands
in a writable section of `game_dx12_ship_replay_dw.exe`.

## 6. Not verified in game (owner test, in this order)

The configured game folder is `E:\IW8\Builds\1.20-replay` (Tool Paths default; no `[tools]` override in
`IW8Radiant.ini`), so this session did not deploy, launch or attach (rule: never write there). Owner steps:

1. Tools > Tool Paths: set **launch** to `game_dx12_ship_replay_local_dw.exe` (with the local DW server already running),
   or to `E:\DW\IW8\Run_Replay_DW_Local_Server.cmd` (starts both; then the mode-select step uses `dw_localTestMenuMP`).
   Leaving it empty starts the stock exe, which cannot load custom maps in this install (section 2).
2. Game closed, map open, F5. Expect in the compile log: `F5: Replay is not running ...`, `started ...`,
   `Hosting a private match lobby (xstartprivatematch)`, `Starting <id> (war) from the lobby`, `In game on <id>.`
   Check: the game boots straight to the MP menu (`+mpMode 1`); a private match lobby opens by itself; the match loads
   the map. If the lobby step fails, open Private Match by hand and press F5 again (the start step from a lobby is the
   one to confirm first: `xpartygo` after `set NSQLTTMRMP` / `set MOLPOSLOMO`).
3. In that match, change a brush, F5. Expect `deploying the rebuild and reloading its zones (map_reload)` then
   `Reloaded <id> from disk; in game.` and the change visible. If instead `files are in use`, the editor ends the match
   (`xpartybackout`), deploys and starts again: confirm the lobby returns and the new build loads.
4. In a match on a stock map you host, F5: expect the match to end to the lobby and the custom map to start.
5. Local Play lobby you host (instead of Private Match), F5: expect `sv_systemlinkStartMatch`.

Open questions: U1 `xstartprivatematch` without the Lua lobby menu (lobby UI may not open, the party may); U2 `set` on
`LSTLQTSSRM` (flags 0x80) from the console; U3 `.ff` handles after load (deploy during a match); U4 rescan of a new
package id at match start; U5 `+mpMode 1` on the DW exes; U6 `xpartybackout` for a Local Play host.
