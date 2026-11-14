# 76. Cold start devmap: from "game closed" to "spawned in TDM" in one key

Date: 2026-11-11. Scope: the fastest reliable way to go from no game process to a private TDM (war) match on a
named custom map, started by one editor action (F5). Analysis and design only.

Rules kept: the game was not started, no process was attached to or written, nothing was written under
`E:\IW8\Builds\`, no source file and no IDA name was changed. This file is the only file created in any project.
One read-only check script was written to the session scratchpad (path in the next section).

Legend: **VERIFIED IN CODE** = read in the binary (IDA address given) or in source (file and line given).
**INFERRED** = follows from verified code, not executed. **NEEDS A LIVE TEST** = only a run can settle it; the test
is in section 5. Every guess is marked "guess".

Sources:
- `[IDA]` the IDA database. It is for the OLDER, plain exe: `game_dx12_ship_replay.exe` (2026-08-25, MD5
  `1c238fe3...5439`, SHA256 `68fb1cbc...32f0`, image base `0x140000000`). It is NOT the exe the owner runs. Read only.
- `[DWEXE]` the exe the owner runs: `game_dx12_ship_replay_local_dw.exe` (2026-11-08, marker `RPLYCMP2` at file
  offset 0x40). Read as a file only (bytes), with names from `[DWMAP]`.
- `[DWMAP]` `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.map` (linker map of the recompiled build, same date).
- `[ELOG]` `E:\IW8\Builds\1.20-replay\mw120rproxy.engine.log` (2026-10-04, the older exe with the old proxy DLL,
  engine prints with millisecond ticks). The only log with a full timed boot and match start.
- `[DWLOG]` `E:\IW8\Builds\1.20-replay\replay-dw-client.log` (the recompiled client, last run today).
- `[DW]` `E:\DW\IW8\integration\replay\` (the source that is compiled into the recompiled exe).
- `[PX]` `E:\mw124\mw120rproxy\mw120rproxy\` (source of the old proxy DLL). Used only as a description of logic.
- `[ED]` `E:\IW8-Radiant\src\` (editor).
- Earlier research: docs 05, 19, 47, 65. Doc 65 is the base. Section 0 lists what this document corrects in it.

State of the install today (VERIFIED by listing the game folder):
- The proxy DLL (mw120rproxy) is NOT installed. Its file is parked as `XInput9_1_0.dll.diag-disabled`, there is no
  `XInput9_1_0.dll`, and its logs were last written on 2026-10-11. Nothing in this design depends on it.
- The owner launches `game_dx12_ship_replay_local_dw.exe`. Its features (custom maps, console, test commands, lobby
  controls) are compiled in from `[DW]`. `[DWLOG]` shows it loading `mp_iw8r_fix1` and running `map_reload` today.
- `.proxy\control.ini` has no reader now: `[DW]` contains no reference to `control.ini`, `.proxy`, the F6 browser or
  `mw_maps`. The editor still writes that file; nothing answers.

### Which exe the addresses belong to

- Every address written as plain `0x14...` in sections 0 to 3 is **from the older build (the IDA database), to be
  re-checked** before anyone uses it against the recompiled exe. The recompiled exe moves code and data.
- The table below gives the address of the same function in the recompiled exe, from `[DWMAP]`. These are the ones
  to use for `game_dx12_ship_replay_dw.exe` and `game_dx12_ship_replay_local_dw.exe`.
- The two recompiled exes have the same code addresses. VERIFIED: a byte compare of the two files finds exactly four
  differing runs of 256 bytes (file offsets 133709035, 133905137, 133905729, 133999947), inside the four ranges that
  `game_dx12_ship_replay_local_dw.setup.json` lists as public key constants (`only_public_key_constants_changed:
  true`). So the `_dw` map and pdb are valid for the `_local_dw` exe.
- The editor's own memory reads are not affected: `core/live_direct` maps older-build RVAs through the `.rplmap`
  section of the running exe.

| Function or data | Older build (IDA) | Recompiled exe (`[DWMAP]`) |
|---|---|---|
| `Com_ParseCommandLine` | 0x1412B03B0 | 0x1412E03B0 |
| `g_Com_CommandLineArgsPtr` (the whitelist) | 0x1445ACE68 | 0x144618E68 |
| `Com_Init` | 0x1412AF040 | 0x1412DF040 |
| `Com_Init_GameMode_FromCommandLine` | 0x1412AF5D0 | 0x1412DF5D0 |
| `Com_ExecStartupConfigs` | 0x1412AA1B0 | 0x1412DA1B0 |
| `Com_CompareStartupCommands` | 0x1412B5140 | 0x1412E6140 |
| `Cmd_ExecuteSingleCommand_Internal` | 0x1412976C0 | 0x1412C76C0 |
| `Com_GameMode_SetDesiredGameMode` | 0x1410C88F0 | 0x1410EF8F0 |
| `Com_GameMode_CommitRequestedMode` | 0x1410C8A30 | 0x1410EFA30 |
| `Com_GameMode_GetActiveGameMode` | 0x1410C86E0 | 0x1410EF6E0 |
| `Com_FrontEndScene_IsActive` | 0x1410C6BD0 | 0x1410ECBD0 |
| `CL_MainMP_StartPrivateMatchHost_f` (`xstartprivatematch`) | 0x1415E4910 | 0x141630910 |
| `CL_MainMP_PartyGo_f` (`xpartygo`) | 0x1415E4470 | 0x141630470 |
| `SV_SystemLinkStartMatch_f` | 0x14136B6F0 | 0x1413A66F0 |
| `PartyHost_RequestStartMatch` | 0x14119CFA0 | 0x1411C5FA0 |
| `PartyHost_UpdateMatchStart` | 0x14119F5D0 | 0x1411C85D0 |
| `PartyHost_StartMatch_Internal` | 0x14119E3D0 | 0x1411C73D0 |
| `Party_RegisterDvars` | 0x14118FDD0 | 0x1411B7DD0 |
| `SV_DevMap_f` | 0x14136BBB0 | 0x1413A6BB0 |
| `SV_DevMapDedi_f` | 0x14136BD70 | 0x1413A6D70 |
| `SV_StartLocalReplay` | 0x14136B940 | 0x1413A6940 |
| server application vtable, slot 15 (devmap) | 0x1424B2188 | 0x14251E188 |
| `Replay_LoadArchiveFile` | 0x140F2FF90 | 0x140F48F90 |
| `Live_ServiceFrameErrors` (DW per-frame hook, runs the test channel) | n/a | 0x1416B9AC0 |
| `DW_LobbyControls_Frame` (DW code) | n/a | 0x15547CE20 |

What was checked directly in the recompiled exe (`[DWEXE]` bytes, read only, script in the session scratchpad
`%USERPROFILE%\AppData\Local\Temp\claude\2b25dc13d65a86cc\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\s76\check_dw.py`):
- The whitelist is there. The three pointers at 0x144618E68 point to `"connect_lobby "`, `"retaildev"`,
  `"devreplay"`. `Com_ParseCommandLine` at 0x1412E03B0 loads that table (`lea r13` at the same offset +0xEE) and has
  the same `cmp ebx,3 / jb / mov byte ptr [rsi],0` bytes at the same offset (+0x18A). VERIFIED IN THE RECOMPILED EXE.
- `Com_Init` at 0x1412DF040 calls, in the same order and at the same offsets as the older build:
  `Com_Init_GameMode_FromCommandLine` (at 0x1412DF144), `Com_GameMode_CommitRequestedMode` (at 0x1412DF149),
  `Com_ExecStartupConfigs` (at 0x1412DF2D6). VERIFIED IN THE RECOMPILED EXE.
- The three devmap slots (`SV_DevMap_f`, `SV_DevMapDedi_f`, `SV_StartLocalReplay`) sit together in `.rdata` at
  0x14251E188. So the devmap function is present and referenced in the recompiled exe too. VERIFIED.
- Twelve functions were compared byte for byte between the two builds (the command line functions, `xpartygo`,
  `xstartprivatematch`, the party start functions, `SV_DevMap_f`, `Com_Init`). Each has the same length and 74 to
  88 percent identical bytes. The differing bytes are what moved addresses would change. So the logic read in IDA is
  the logic in the recompiled exe (INFERRED from that compare; the decompiled text itself is from the older build).
- Not re-checked in the recompiled exe: everything else read in IDA (for example the countdown dvar registrations
  and `DevCmdSetup`). Those stay "from the older build, to be re-checked".

---

## The answer in one paragraph

The exe to start is `game_dx12_ship_replay_local_dw.exe`, and the local online-services host on 127.0.0.1 must be
up first; today F5 would start the older exe and never checks the host. Keep the method the editor already has
(start the exe, attach, send console commands), but fix those two things and three more that make it slow or
wrong, then move the sequence into the client (the recompiled exe's own source; there is no proxy DLL). The game cannot be told what to do on the command
line: this build throws away every `+command` except three (`connect_lobby`, `retaildev`, `devreplay`), so
`+mpMode 1`, `+set` and `+devmap` do nothing. There is no `devmap` or `map` console command in Multiplayer, although
the real devmap function is still in the exe. The working start is the lobby start: host a private match
(`set LSTLQTSSRM 1`, `xstartprivatematch`), set `ui_mapname` and `ui_gametype`, then `xpartygo 1`. The `1` is new
and matters: it skips the 8 second lobby countdown. Step 1 is editor only and useful on its own: send `xpartygo 1`,
stop waiting fixed times (15 s, 5 s) and wait for real signals instead. Step 2 is a small client addition: read a
start request from the process command line, switch to Multiplayer inside `Com_Init` (what `+mpMode 1` used to do),
and run the same lobby start in process on the first frame it is possible. The unavoidable time is zone loading
(about 20 s for `global_stream_mp` with its shader pipelines, about 35 to 50 s for the `common_mp` zones in the one
timed run, about 7 s for a small map). The design removes about 25 to 30 s of fixed waits and all menu clicks.

---

## 0. Corrections to doc 65

| Doc 65 says | What the code shows | Evidence |
|---|---|---|
| `+set <dvar> <value>` works at boot; F5 launches with `+mpMode 1` to skip mode select | `Com_ParseCommandLine` keeps a `+` token only when it starts with one of three strings. Every other token is blanked. `+mpMode 1` and `+set ...` are dropped before anything reads them | `[IDA]` 0x1412B03B0, table 0x1445ACE68, blanking store at 0x1412B053F. Section 2.1 |
| `SV_DevMap_f` has no reference in the image | It is slot 15 of the Multiplayer server application vtable (pointer at 0x1424B2188). No registered command calls that slot | `[IDA]` data xref from 0x1424B2188 to 0x14136BBB0. Section 2.2 |
| Start command is `xpartygo` | `xpartygo 1` sets the "skip countdown" flag. Without the argument the lobby waits 5 + 3 seconds | `[IDA]` 0x1415E4470, 0x14119F5D0. Section 2.3 |

Everything else in doc 65 that was re-read here holds (lobby start path, gates, state RVAs).

---

## 1. The sequence, game closed to spawned in TDM

This is the recommended final sequence (client addition in place). The last column says what the editor's
current F5 does at that step.

| # | Step | Evidence | Status | Today (doc 65 F5) |
|---|---|---|---|---|
| 1 | Editor compiles, then deploys the package to `mods\mw120r\maps\<id>\` while no game holds it | `[ED]` `qtapp/main_window.cpp:3053-3077`, `qtapp/devmap_launch.cpp:232-236` | VERIFIED IN CODE | same |
| 2 | Editor makes sure the local online-services host runs: `GET http://127.0.0.1:8085/health` must answer with `server` = `iw8-dw`. If not, it starts it (`E:\DW\IW8\Host-Local-Server.ps1 -NoLaunch`) and waits for the health answer | `E:\DW\IW8\Host-Local-Server.ps1:33-34,98-120,149` (server exe `build\Release\iw8-dw-server.exe`, ports 8085, 8443, 3075, `-NoLaunch`); the exe's ini has `host=127.0.0.1`; `[DWLOG]` `DW_LOCAL_PROBE_OK host=127.0.0.1 backend health reached` | VERIFIED IN CODE (scripts and log); the editor does not do it yet. Section 3 Q1 | not done |
| 3 | Editor starts `game_dx12_ship_replay_local_dw.exe`, working directory = game folder, with a start request on the command line (proposed: `--devmap <id> --gametype war`) | Working directory: doc 19 section 2.2. The engine ignores text that is not a whitelisted `+` token (step 4), so extra arguments are harmless | INFERRED | starts the Tool Paths exe with `+mpMode 1`, which is dropped. With the owner's settings that exe is the OLDER one (section 3 Q1) |
| 4 | Engine parses the command line. Only `+connect_lobby `, `+retaildev`, `+devreplay` survive | `[IDA]` `Com_ParseCommandLine` 0x1412B03B0 | VERIFIED IN CODE | n/a |
| 5 | Client switches to Multiplayer inside `Com_Init`, before the first frame: it calls `Com_GameMode_SetDesiredGameMode(2, 1)` where the stock code calls `Com_Init_GameMode_FromCommandLine`. `Com_Init` commits the mode right after | `[IDA]` `Com_Init` 0x1412AF040 calls 0x1412AF5D0 at 0x1412AF144 and `Com_GameMode_CommitRequestedMode` 0x1410C8A30 at 0x1412AF149. `Com_GameMode_SetDesiredGameMode` 0x1410C88F0 has one gate: no other mode active | VERIFIED IN CODE (the native path); the client change is new, NEEDS A LIVE TEST | waits 15 s on the mode select screen, then sends `dw_localTestMenuMP` |
| 6 | Client scans custom packages. No selection step is needed on the DW client | `[DW]` `DW_MapAvailability.cpp:117,392,746-785` (scan in `Com_GameInfo_Init` and on the first DW frame); `[DWLOG]` `DW_CUSTOM_MAPS installed: ... mp_iw8r_fix1 ...` | VERIFIED IN CODE | same |
| 7 | Boot zones load: `code_post_gfx`, `global`, `ui`, `global_stream_mp` (with pipeline state building), `global_mp`, `mp_frontend3`, then sign-in and the `common_mp` zones | `[ELOG]` timeline, section 3 Q7 | VERIFIED (one logged run) | same |
| 8 | Ready test, every frame, in process: game mode is 2, front end scene active, player signed in, not in a lobby | `Com_GameMode_GetActiveGameMode` 0x1410C86E0, `Com_FrontEndScene_IsActive` 0x1410C6BD0 (doc 65 section 5); `[DW]` `DW_LobbyControls.cpp:591` uses the same two calls | VERIFIED IN CODE (the calls); the exact earliest frame NEEDS A LIVE TEST | polls every 500 ms from outside, then waits a fixed 5 s |
| 9 | Host a private match lobby: `xblive_privatematch` = 1, then `xstartprivatematch` | `[IDA]` `CL_MainMP_StartPrivateMatchHost_f` 0x1415E4910: stops the party, `Party_Awake`, `PartyHost_StartParty_0` with `party_maxplayers`. No sign-in or menu gate inside the function | VERIFIED IN CODE; that the lobby is usable without the Lua lobby menu NEEDS A LIVE TEST (doc 65 U1) | same commands, sent from outside |
| 10 | When we host a game lobby: set `ui_mapname` = id, `ui_gametype` = `war`, optional bots, then request the start with skip countdown | `[IDA]` `PartyHost_RequestStartMatch` 0x14119CFA0 (party running, in party, host, game lobby or system link lobby), second argument = skip countdown. `[DW]` already has this call: `DW_LobbyControls.cpp:745-750` (`replay_dw_forceStart`) | VERIFIED IN CODE | sends `xpartygo` (no argument): 8 s countdown |
| 11 | Lobby update sees the request, countdown 0, calls `PartyHost_StartMatch_Internal` | `[IDA]` `PartyHost_UpdateMatchStart` 0x14119F5D0: `m_gameStartSkipCountdown` makes both timers 0 | VERIFIED IN CODE | waits 5 + 3 s |
| 12 | Match start: reads `ui_mapname` through `Party_GetMapName`, checks the content pack, loads the map zones, `Com_GameStart_BeginServer(map, gametype)` | `[IDA]` `PartyHost_StartMatch_Internal` 0x14119E3D0, call to 0x1410C8D30 at 0x14119E67A | VERIFIED IN CODE; custom zones routed by the DW client: doc 65 section 2 | same |
| 13 | Player spawns. Editor sees "local player exists and `ui_mapname` = id" | `[ED]` `core/devmap_plan.cpp` `decide()` WaitMatch; `[ELOG]` `ui_player_spawned_notify` 7.5 s after the start | VERIFIED (one logged run on `mp_test`) | same |

---

## 2. What was verified in the binary this session

All addresses in this section are VAs **from the older build (`[IDA]`), to be re-checked**. The matching addresses
in the recompiled exe, and what was confirmed there, are in the table near the top.

### 2.1 Command line

- `Com_ParseCommandLine` 0x1412B03B0. Called once, from `Com_Init_Try_Block_Function` (0x1412AF930) at 0x1412B0012.
  It splits the text at `+`, line feed and the end (bit mask `0x80000000401`), trims spaces and tabs, keeps at most
  128 tokens. Then, for each token, it compares the start of the token (case insensitive, length of the list entry)
  with the three entries of `g_Com_CommandLineArgsPtr` (0x1445ACE68):
  - 0x14248A6F0 `"connect_lobby "` (with the trailing space)
  - 0x14248A700 `"retaildev"`
  - 0x14248A710 `"devreplay"`

  A token that matches none gets its first byte set to 0 (`mov byte ptr [rsi], 0` at 0x1412B053F). There is no
  condition around this loop. VERIFIED IN CODE.
- `Com_Init_GameMode_FromCommandLine` 0x1412AF5D0 reads the same token list and looks for `coreMode`, `mpMode`,
  `aliensMode`, `cpMode`, `spMode`, `noMode` as the FIRST word of a token. A blanked token has no words. So
  `+mpMode 1` can never reach it in this build. VERIFIED IN CODE.
- `Com_ExecStartupConfigs` 0x1412AA1B0 (called from `Com_Init` at 0x1412AF2D6) skips empty tokens and runs each
  remaining token through `Cmd_ExecuteSingleCommand_Internal` (0x1412976C0), split at `;` outside quotes and at
  line ends. VERIFIED IN CODE. Consequence (INFERRED, NEEDS A LIVE TEST): a token that starts with a whitelisted
  word can carry more commands after a semicolon, for example `+retaildev 0;set LSTLQTSSRM 1`. `retaildev` is
  harmless: its handler `Cmd_SetStringDvarFromArg1` 0x1413FE9F0 (table entry at 0x144618F00) sets the string dvar
  `retail_dev` to its argument. This is a side door, not something to build on: the commands run at the end of
  `Com_Init`, in game mode 0, before any Multiplayer command exists.
- `Com_CompareStartupCommands` 0x1412B5140 sorts a token whose first word starts with `devmap` last. It is the only
  code that references the string `devmap`. A leftover of the developer build.
- `devreplay` (table at 0x1445BE900, registered by 0x14132CB10 from `Com_Init_Try_Block_Function` at 0x1412B0192)
  runs 0x14132CA30: set desired mode `mp`, commit the mode, then call slot 17 of the active server application,
  `SV_StartLocalReplay` 0x14136B940. That function calls `Replay_LoadArchiveFile` 0x140F2FF90, which raises
  `Com_Error` when the replay archive file (version 4, 148 byte header) or its playlist file is missing. After that
  it starts the map named by `ui_mapname` with `Com_GameStart_BeginDevmap` 0x1410C8D10. So `+devreplay` is a real
  "boot straight into a map" path, but it needs a valid replay archive. Not usable without forging that file. Not
  recommended.
- Intro and splash: the bool dvar `intro` ("Intro movie should play") is registered at 0x1412B2821 into
  `dvar_intro`, and nothing reads `dvar_intro` (one xref, the write at 0x1412B282D). Dead in this build. The dvars
  that matter are `cg_viewedSplashScreen` (`MLNMPQOON`), `cg_autoSkipSplashScreen` (`NPLRKNKKOP`) and
  `lui_skip_boot_flow` (`LKSQOLNKLP`). The old proxy DLL forced the first and the third at registration (`[PX]`
  `dvar_patches.cpp:243,254`); that DLL is not installed. What the recompiled exe does with them was not found in `[DW]` (it only adds
  `lui_dwIntroVideos`, default false, `DW_LobbyControls.cpp:501`).

### 2.2 The devmap function that still exists

- `SV_DevMap_f` 0x14136BBB0, a method of the Multiplayer server application (vtable slot 15, pointer at
  0x1424B2188; `SV_DevMapDedi_f` 0x14136BD70 is slot 16 at 0x1424B2190; `SV_StartLocalReplay` is slot 17 at
  0x1424B2198). VERIFIED IN CODE.
- What it does, in order: read argument 1 as the map name; `SV_Cmds_CheckMapCmdValid` 0x14132CAB0 (in MP the name
  must pass `Com_GameMode_SupportsMap` 0x1410C89D0); `SvGameModeAppMP::DevCmdSetup` 0x14136B720; set
  `systemlink_host`; take the game type from `Party_GetGametype` (`ui_gametype`);
  `SV_CmdsMP_StartMapWithSettings` 0x14136C6B0; `SV_MainMP_PumpServerStartup` 0x14137EA20; then loop until the
  NETWORKING fence passes (`FenceManager_IsFenceTypeActive(controller, 4)`). `Live_IsOfflineTool` 0x141528470
  returns 0, so that wait always runs.
- `DevCmdSetup`: `Live_InitForDevmap` 0x141665CB0 (when not signed in: gamertag from `%USERNAME%`, random xuid),
  shut down the front end scene, `Com_Shutdown("EXE/SERVERKILLED")`, stop any party, `onlinegame` 0, `systemlink` 1,
  `IsDevmapping` 1, restart the network if needed, `Party_Awake` + `PartyHost_StartPrivateParty`, UI root = system
  link lobby. It needs no lobby, no menu and no private match state. It builds its own.
- No command reaches slot 15. The server command list registered at startup has two entries (`devreplay`,
  `devlocalreplay`), and the Multiplayer command tables listed in doc 65 section 1 have no `devmap`.
- So: `devmap <map>` does not exist as a console command. The function behind it does. A client that can call
  native functions (the DW client can) could register a `devmap` command that tokenizes its arguments as a server
  command and calls slot 15. Whether the function still works end to end in a ship build is unknown: it has had no
  caller since the developer build. NEEDS A LIVE TEST (test T5). It is the only path that skips the lobby.

### 2.3 The lobby start

- `xstartprivatematch` = `CL_MainMP_StartPrivateMatchHost_f` 0x1415E4910. Only check: local client 0.
- `xpartygo [n]` = `CL_MainMP_PartyGo_f` 0x1415E4470. Refuses unless `xblive_privatematch` (`LSTLQTSSRM`) is true
  and the session is valid. `n` non zero = skip countdown. Calls `PartyHost_RequestStartMatch(party, skip)`.
- `sv_systemlinkStartMatch` = 0x14136B6F0. Needs `systemlink_host`. Always passes skip = 0.
- `PartyHost_RequestStartMatch` 0x14119CFA0 prints one of four refusals (party inactive, not in a party, not the
  host, not a game lobby) or `RequestStartMatch succeeded`.
- `PartyHost_UpdateMatchStart` 0x14119F5D0: countdown = 1000 ms x (A + B). For private and system link lobbies
  A = `pt_privatePregameStartTimerLength` (`LTLOQSNSOT`, default 3) and B = `pt_privateGameStartTimerLength`
  (`NMQNNPQPRS`, default 5), both int, range 0 to 60, flags 0, registered in `Party_RegisterDvars` 0x14118FDD0
  (calls at 0x141190098 and 0x14119006B). With the skip flag both are taken as 0. `[ELOG]` confirms the 8 s:
  `RequestStartMatch succeeded` at 128.2 s, `Setting ui_maxclients ... in StartMatch()` at 136.2 s.
- The DW client already exposes the skip start as a dvar: `set replay_dw_forceStart 1` calls
  `PartyHost_RequestStartMatch(party, true)` once, when the front end is active and we are in a lobby
  (`[DW]` `DW_LobbyControls.cpp:493-494,587-591,745-750`). It must go back to 0 before it can fire again.

---

## 3. Answers to the seven questions

### Q1. Launch: command line, working directory, environment, files, what can be skipped

- Exe: `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_local_dw.exe` (owner confirmed). Working directory: the
  game folder. The older `game_dx12_ship_replay.exe` has no custom map support at all now (no proxy).
- **How the editor picks the exe today, and why that is wrong for F5.** `MainWindow::toolPath`
  (`[ED]` `qtapp/main_window.cpp:3893-3903`) reads `tools/<key>` from the settings, with a built-in default of
  `E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe` for `replay`. F5 and Launch Replay use
  `toolPath("launch")`, and when that is empty, `toolPath("replay")` (`main_window.cpp:3073,3085`). The owner's
  `bin/release/IW8Radiant.ini` has no `[tools]` section (VERIFIED by reading it), so both are at their defaults and
  F5 would start the OLDER exe, which cannot load the package. The game folder (deploy target, process filter, log
  path) is taken from the folder of `replay`, which is the right folder. `core/setup_detect.*` can already tell a
  recompiled exe (`RPLYCMP2` at file offset 0x40) from the older one and list every `game_dx12_ship_replay*.exe` in
  a folder (`setup_detect.h:17-23,45`). The design uses that: see step 1 item 6.
- What must already be running: the local online-services host. The exe's ini points it at `host=127.0.0.1`. The
  client probes it at start (`[DW]` `DW_LocalBackend_Frame.cpp:248`: `DW_LOCAL_PROBE_OK host=...` or
  `DW_LOCAL_PROBE_FAILED host=...`) and signs in through it (`DW_LocalPlatform.cpp:781`:
  `DW_LOCAL_PLATFORM_FAILED response unavailable, invalid, expired ...` when it cannot). The host is
  `E:\DW\IW8\build\Release\iw8-dw-server.exe "<data folder>"`, started by `E:\DW\IW8\Host-Local-Server.ps1`
  (ports 8085, 8443 and 3075 must be free; health at `http://127.0.0.1:8085/health`, ready when `server` is
  `iw8-dw`; `-NoLaunch` starts the host and refreshes the local client without starting the game;
  `Run_Replay_DW_Local_Server.cmd` runs the same script and then starts the game with no arguments).
  How far the game gets without the host (menu or not) was not determined; `xpartygo` needs a valid session, so a
  private match start without sign-in is unlikely (guess). The cold start must treat the host as required.
- There is no `E:\IW8-Radiant\tools\live`. The `live.py` named in the brief is
  `E:\mw124\mw120rproxy\tools\live\live.py`. It is a client for the recompiled exe's development command file
  (`replay-dw-test.command`, polled every 500 ms on the main thread when `replay-dw-test.enabled` existed at
  launch). It is not the online-services host.
- Files the client needs beside the exe: `game_dx12_ship_replay_local_dw.ini` (`host=127.0.0.1`,
  `bypass_update_restart=1`; present), and `replay-dw-test.enabled` only if the test commands
  (`dw_localTestMenuMP`, the command file) are wanted (`[DW]` `DW_LocalPlatform.cpp:427-441,1995-2003`; present).
- Environment: none required. The DW pair launcher sets `LOCALAPPDATA` only to isolate a second profile
  (`launch-native-pair.ps1:110-112`). Not needed for one client.
- Command line switches that exist in THIS build: only the three whitelisted startup commands (section 2.1).
  `+set`, `+exec`, `+map`, `+devmap`, `+mpMode` are dropped. The mode words (`mpMode` and the others) exist in
  `Com_Init_GameMode_FromCommandLine` 0x1412AF5D0 but cannot be reached. The DW launcher's own
  `+set LKOTRPTMKO <port>` (`launch-native-pair.ps1:122`) is, by the same reading, dropped (INFERRED; the port
  also comes from the ini).
- Skippable things and how:
  - Mode select screen: not skippable from the command line. Today: `dw_localTestMenuMP`. Proposed: the client sets
    the desired mode in `Com_Init` (step 5).
  - Splash and boot flow: dvars `cg_autoSkipSplashScreen` (`NPLRKNKKOP`), `cg_viewedSplashScreen` (`MLNMPQOON`),
    `lui_skip_boot_flow` (`LKSQOLNKLP`). Effect on the DW client: NEEDS A LIVE TEST (T4). They would have to be set
    by the client at registration or through the semicolon side door.
  - Intro video dvar `intro`: dead (section 2.1).
  - First run prompts (EULA, offensive content `profileMenuOption_offensiveContentMode`): stored in the player
    profile, already answered on this install. Not looked at further.
  - Shader pipeline build: not skippable. It is part of loading `global_stream_mp` (`[ELOG]`: "Building PSOs 24099
    (21377 shared, 2722 cached)" between 13.7 s and 31.5 s). The engine reads `.psob` cache files, so the cost is
    the build of what is not cached.
  - Online sign-in: on the DW client it is local and takes about 2.4 s (`[DWLOG]` `DW_LOCAL_PLATFORM_BEGIN` to
    `DW_LOCAL_IDENTITY_CLAIMS_RESTORED`, ticks 49389296 to 49391718). In the stock run the SIGNED_IN fence passed
    at 37.8 s and NETWORKING at 43.4 s (`[ELOG]`).

### Q2. Is there a real `devmap` / `map` / `map_restart`?

- `map`, `devmap`: no command in Multiplayer. `SV_DevMap_f` exists and is complete (section 2.2), unreachable from
  the console. `map`, `spmap`, `map_restart`, `fast_restart` are single player only (doc 65 section 1).
- `map_restart`, `fast_restart`, `map_refresh`, `map_reload`: added by the DW client for a running host
  (`[DW]` `DW_MapRefresh.cpp:652-655`). They need a running match. Not a cold start tool.
- The game's own "start private match" path, callable by console command: section 2.3. Preconditions, in order:
  Multiplayer mode active; a party we host that is a game lobby (made by `xstartprivatematch`);
  `xblive_privatematch` true for `xpartygo`; valid session; then the map must pass
  `Content_DoWeHaveContentPack(Live_GetMapSource(Live_GetMapIndex(map)))` inside
  `PartyHost_StartMatch_Internal`, or the party is stopped with the missing map error. Custom packages pass this on
  the DW client today (`[DWLOG]`, matches on `mp_iw8r_fix1`).
- No cheat gate, no `sv_cheats` check and no online gate was seen in either path.

### Q3. The earliest safe moment, and how to know it

- Cheapest reliable signal from outside (already built): the four memory reads in `[ED]` `core/devmap_plan.h`:
  game mode byte (RVA 0x0C5244E0) = 2, front end int (0x0C4EBEB0) >= 5, `g_partyData_inParty` (0x0C7E23CC),
  `g_partyData_areWeHost` (0x0C7E23C8). They are readable as soon as the process lists its modules.
- Missing today: a sign-in signal. The editor replaces it with fixed waits (15 s on mode select, 5 s on the menu).
  Use the log instead: `DW_LOCAL_PLATFORM_SIGNED_IN` (time stamped, `[DW]` `DW_LocalPlatform.cpp`, seen about
  0.2 s after the first frame in `[DWLOG]`) and `DW_LOCAL_IDENTITY_CLAIMS_RESTORED` (about 2.2 s later). The runner
  already tails this log (`[ED]` `qtapp/devmap_launch.cpp:187-197`).
- Fallback signal: `DW_CUSTOM_MAPS installed:` in the same log (the package scan ran; the id must be in the list),
  then the state reads with a short settle.
- In process (the proposed client code) no outside signal is needed: test the conditions of step 8 every frame.
- Not determined statically: whether `xstartprivatematch` is safe while the `common_mp` zones are still loading.
  In `[ELOG]` the menu itself started a private party at 47.5 s while `common_mp` was still loading (until 98 s)
  and nothing failed. Guess: it is safe. NEEDS A LIVE TEST (T3 measures it).

### Q4. Custom maps: selection before the menu

- DW client: there is no selection. Every package found by the scan is routed by file name
  (`[DW]` `DW_MapAvailability.cpp`, doc 65 section 2). The scan runs at boot. A package deployed before launch is
  always known. Nothing to do.
- The old proxy's `SelectPackage` (`[PX]` `developer_ui.cpp:142-186`) existed because the proxy only routed the
  files of one selected package. That logic was not carried into the recompiled exe: `[DW]` has no select step, no
  F6 browser and no control file reader. The recompiled exe routes every scanned package. So the question "can the
  selection be made before the menu" has the answer "there is nothing to select".
- The only thing that names the map is `ui_mapname`, and it is read at match start (`Party_GetMapName`).

### Q5. Fewest settings for a solo TDM match that starts at once

Sent as console text, tokens only (the command buffer path does not translate names):

| Purpose | Command | Token meaning | Status |
|---|---|---|---|
| Allow `xpartygo` | `set LSTLQTSSRM 1` | `xblive_privatematch` | VERIFIED IN CODE (gate in 0x1415E4470) |
| Host a lobby | `xstartprivatematch` | | VERIFIED IN CODE; NEEDS A LIVE TEST without the menu |
| Map | `set NSQLTTMRMP <id>` | `ui_mapname` | VERIFIED IN CODE (`Party_GetMapName`) |
| Mode | `set MOLPOSLOMO war` | `ui_gametype` | VERIFIED IN CODE |
| Start now | `xpartygo 1` | skip countdown | VERIFIED IN CODE; NEEDS A LIVE TEST |
| Start now, Local Play lobby | `set NMQNNPQPRS 0`, `set LTLOQSNSOT 0`, `sv_systemlinkStartMatch` | the two countdown timers | INFERRED (range 0 to 60, flags 0); NEEDS A LIVE TEST |
| Bots, optional, DW client | `set replay_dw_botCount <n>` before the start | DW dvar, 0 to 199 | VERIFIED IN CODE (`DW_LobbyControls.cpp:497-498`), effect not tested here |
| Bots, stock dvars | `bot_MaxNumEnemyBots` `RNRLKSNNR`, `bot_MaxNumAllyBots` `SNMLPONRP`, `mp_bots_sec_before_lobby_autofill` `NLLMRMSTLT` | doc 47 | names VERIFIED, effect unverified |

- Minimum player wait: none seen in the lobby path for a private game. The solo Local Play start in `[ELOG]`
  went through with one player. `party_minplayers` (`LLNLRTQRPO`) exists; not needed there.
- In-match pre-match countdown ("match begins in"): script side. The timer dvars the scripts read are not in the
  engine's dvar name table, so their tokens could not be found statically. In `[ELOG]` the player spawned at 6.35 s
  of server time on `mp_test`. If a countdown is seen after spawn, it has to be found in the game type scripts.
  Not determined.

### Q6. The alternatives

| | (a) command line only | (b) command line + editor attach (exists) | (c) client reads a start request and does it in process |
|---|---|---|---|
| Works today | No. The whitelist drops everything useful, and no Multiplayer command exists at the time startup commands run | Yes in design; not confirmed in game (doc 65 section 6) | Needs client code |
| Speed | n/a | Loses time to polling (500 ms), fixed waits (15 s + 5 s + 1.5 s) and the 8 s countdown. After step 1 below: about 3 to 5 s of slack (guess) | No slack: acts on the exact frame. Also skips the mode select screen and its work |
| Robustness | n/a | Writes the command buffer from another process without the game's lock (`[ED]` `core/live_direct.h` header says so). Depends on RVAs and the `.rplmap`. Timing by guess | Main thread, native calls, no cross process writes. One request per process, no stale state |
| Cost | n/a | Small editor edits | One client file plus small editor edits |

Recommendation: (c), reached through (b). First make (b) fast and correct, because it needs no client build and it
stays as the fallback for a client that does not know the request. Then add (c).

Why not rely on the semicolon side door for (a): it only runs commands at the end of `Com_Init`, where there is no
Multiplayer mode, no lobby and no `xpartygo`. It could preset dvars, but game mode dvars are deregistered and
registered again on a mode switch (`Com_GameMode_CommitRequestedMode` calls `Dvar_DeregisterGamemodeDvars`), so
even that is uncertain.

Why not the real devmap now: it is the fastest on paper (no lobby), but it is untested dead code with a blocking
wait on the NETWORKING fence. Keep it as an experiment (T5) after (c) works.

### Q7. Where the time goes

One timed run exists: `[ELOG]`, stock exe plus proxy, 2026-10-04, engine diagnostics on (so slower than normal),
map `mp_test`, Local Play. Seconds from the first log line:

| t (s) | Event | Kind |
|---|---|---|
| 0.5 | `IW8 8.19 build`, `Boot arguments: ''`, init | process start |
| 1.8 | window thread | process start |
| 3.3 to 6.9 | `code_post_gfx` zones (2.0 s + 1.5 s) | zone load |
| 7.0 | front end requested (`mp_frontend3`); 7.5 `splash_screens` menu | UI |
| 7.5 to 12.2 | `global` (0.5 s), `ui` (4.3 s) | zone load |
| 12.4 to 13.7 | `techsets_global_stream_mp` (1.3 s) | zone load |
| 13.7 to 34.0 | `global_stream_mp` (20.2 s), with "Building PSOs 24099 (21377 shared, 2722 cached)" | zone load + shader pipelines |
| 34.0 to 37.0 | `global_mp` (1.4 s), `mp_frontend3` (1.1 s) | zone load |
| 37.8 | SIGNED_IN fence passes; 43.4 NETWORKING fence passes | sign-in |
| 45.1 | `ModeFencePopup` (mode select reached) | UI |
| 46.3 | Multiplayer client init | mode switch |
| 46.4 to 98.5 | `common_mp` (reported 8.5 s then 19.0 s), `common_base_mp` (16.0 s), `common_core_mp` | zone load |
| 98.5 to 127.6 | the owner walks the menus | human |
| 128.2 | `RequestStartMatch succeeded` | start |
| 136.2 | `StartMatch()` after the 8 s countdown | fixed wait |
| 136.2 to 140.7 | map load, connection state 9 | map load |
| 143.7 | `ui_player_spawned_notify` | spawned |

Unavoidable (as far as the code shows): process start (about 3 s), boot zones (about 31 s, most of it
`global_stream_mp` with its pipeline build), sign-in (about 2 s on the DW client), the `common_mp` zones (about
35 to 50 s here; guess: much less with a warm disk cache), the map itself (about 7.5 s for `mp_test`).

Removed by the design:
- mode select wait: 15 s fixed today, 0 with the client change;
- menu settle: 5 s fixed today, about 1 s with a real signal, 0 in process;
- lobby countdown: 8 s, 0 with `xpartygo 1`;
- polling slack: up to 0.5 s per step today;
- all human time (29 s in the logged run).

Not known: the same timeline for the DW client. Its log has no time on zone lines. Test T3 measures it.

---

## 4. Implementation plan

Order matters: step 1 is useful alone and needs no client build.

### Step 1. Editor only: make the existing F5 fast and correct

Files: `src/core/devmap_plan.h`, `src/core/devmap_plan.cpp`, `src/qtapp/devmap_launch.cpp`,
`tests/devmap_plan_tests.cpp`.

1. `startMatchCommands` (`devmap_plan.cpp:73-78`): send `xpartygo 1`. For the Local Play branch, put
   `set NMQNNPQPRS 0` and `set LTLOQSNSOT 0` before `sv_systemlinkStartMatch`.
2. Add a sign-in fact to `Context` (for example `signedIn`), filled in `DevmapRunner::readDwLog` from the log text
   after the launch offset: true once `DW_LOCAL_IDENTITY_CLAIMS_RESTORED` (or `DW_LOCAL_PLATFORM_SIGNED_IN`) appears.
   Record the log size at launch so old lines do not count.
3. `decide()` WaitBoot, mode select branch (`devmap_plan.cpp:222-228`): send `dw_localTestMenuMP` when
   `signedIn` and the state has been stable for about 2 s (guess, tune in T3), instead of `kModeSelectSettleMs`
   = 15 s (`devmap_plan.h:131`). Keep the three tries.
4. `kFrontEndSettleMs` (`devmap_plan.h:144`) 5000 to about 1500 (guess, tune in T3). If no lobby after 10 s, send
   the host commands once more before failing.
5. `kTickMs` 500 to 100 while a run is active (`devmap_launch.cpp:17`). The reads are a few `ReadProcessMemory`
   calls.
6. Launch (`devmap_launch.cpp:144-175`, and `main_window.cpp:3073,3085`):
   - Exe choice. When Tool Paths "launch" is empty, do not fall back to `replay` blindly. Look in the game folder
     with `setup_detect` and prefer, in this order: `game_dx12_ship_replay_local_dw.exe`, then any other recompiled
     exe (`RPLYCMP2`), and only then the older exe, with a warning in the F5 log that the older exe cannot load
     custom maps. Better still, set `tools/launch` to the local exe in the first-run setup so the choice is visible
     in Tools > Tool Paths.
   - Online-services host. Before starting the exe, `GET http://127.0.0.1:8085/health`. If the answer is not
     `server` = `iw8-dw`, start the host with a new setting `devmap/hostCommand` (proposed default:
     `powershell -NoProfile -ExecutionPolicy Bypass -File E:\DW\IW8\Host-Local-Server.ps1 -NoLaunch`), wait up to
     60 s for the health answer, and stop with a clear message if it does not come. Do not use
     `Run_Replay_DW_Local_Server.cmd` for this: it starts the game itself, with no arguments, and stops the host
     when the game exits.
   - Start the exe directly (not through a script), so the start request arguments of step 2 reach it.
7. Leave `kDefaultLaunchArgs` as it is for now. It is harmless (the token is dropped) and becomes real after step 2
   if the client is made to accept it. Fix the comment at `devmap_plan.h:88-90`, which says it works.
8. Log the time of each phase change in the F5 status lines (ms since launch). This is the measuring tool for T3.

### Step 2. Client: boot straight into Multiplayer

Where: `[DW]`, one new small file (for example `DW_Devmap.cpp`) or `DW_LocalPlatform.cpp`.

- Read the process command line once (the dedicated console already does this with `GetCommandLineW`,
  `E:\DW\IW8\integration\dedicated\ReplayDedicatedConsole.cpp:125-126`). Parse `--devmap <id>`,
  `--gametype <name>` (default `war`), `--bots <n>` (default 0). Validate the id like the package rule
  (`mp_` prefix, 4 to 15 characters, `[a-z0-9_]`).
- Provide the client's own `Com_Init_GameMode_FromCommandLine` (native 0x1412AF5D0, called once from `Com_Init`):
  when a request is present, call `Com_GameMode_SetDesiredGameMode(2, true)`. `Com_Init` commits it on the next
  line. Without a request, do nothing (same as today, since the stock function never matches).
- Alternative with the same effect: provide the client's own `Com_ParseCommandLine` without the whitelist loop.
  Then `+mpMode 1` works as written and `+set` works again. Simpler, but it reopens every startup command. The
  owner should choose. Guess: the first form is the safer one.
- Both forms are source changes in `[DW]` followed by a rebuild of the recompiled exe (and the local exe refresh
  that `Host-Local-Server.ps1` does). There is no DLL to add this to.

### Step 3. Client: do the lobby start in process

Where: `[DW]` `DW_LobbyControls.cpp`, in `DW_LobbyControls_Frame` (main thread, already holds the lobby party
pointer and the binding to `PartyHost_RequestStartMatch`, lines 30 and 483-600).

State machine, one step per frame, each step logged with `DW_LocalBackend_LogLine` so the editor can follow it:

1. Wait: mode 2, `Com_FrontEndScene_IsActive()`, signed in, not in a lobby. Log `DW_DEVMAP waiting ...` once.
2. Set `xblive_privatematch` true and run `xstartprivatematch` (through the command buffer, or call
   `CL_MainMP_StartPrivateMatchHost_f`). Log `DW_DEVMAP hosting`.
3. Wait: in a lobby, we are host. Check the id is in the scanned list (`DW_MapAvailability_InstalledIds`); if not,
   log `DW_DEVMAP failed: <id> is not installed` and stop.
4. Set `ui_mapname` and `ui_gametype`, set `replay_dw_botCount` when asked, call
   `PartyHost_RequestStartMatch(party, true)`. Log `DW_DEVMAP start requested <id> <gametype>`.
5. Done after the first request, or after a time limit (30 s per wait; log the reason). Never repeat: one request
   per process.

Editor side for step 3 (`src/qtapp/devmap_launch.cpp`, `src/core/devmap_plan.*`): pass the three arguments on
launch; in WaitBoot, if a `DW_DEVMAP` line appears after the launch offset, stop sending commands and only watch
for "in match on the id" or a `DW_DEVMAP failed` line. If no `DW_DEVMAP` line appears within a few seconds of the
sign-in line, fall back to the step 1 path (old client).

### Step 4. Optional experiment: the real devmap

Where: `[DW]`, same file as step 2. Register a command `devmap` that puts its arguments into the server command
tokenizer and calls slot 15 of `SvGameModeApplication::GetActiveServerApplication()` (`SV_DevMap_f`). Only after
test T5 passes should the step 3 state machine use it in place of the lobby.

---

## 5. Live tests for the owner, in order

Each test says what to do and what a pass looks like. Use the DW client with the local backend running.

**T1. The command sequence, by hand (5 minutes, no code change). Run this first.**
Start the game normally. Go to the Multiplayer menu and stop there (do not open Private Match). Open the console and
type, one line at a time:
`set LSTLQTSSRM 1` then `xstartprivatematch`. Pass A: a private match lobby that you host appears (or, if the
screen does not change, the editor's Live Link dock shows "lobby (host)"). Then type `set NSQLTTMRMP <your map id>`,
`set MOLPOSLOMO war`, `xpartygo 1`. Pass B: the loading screen starts at once, with no countdown, and you spawn on
the map in TDM. If A fails, open Private Match by hand and repeat only the second part: that still proves B.
This one test covers every variant in this document.

**T2. The command line filter (2 minutes).**
Start `game_dx12_ship_replay_local_dw.exe +mpMode 1` from a shortcut. Expected by this analysis: the mode select
screen still appears (the token is dropped; the filter was found in the recompiled exe's bytes). If the game goes
straight to Multiplayer instead, this analysis is wrong somewhere: report it, because then step 2 is not needed.
Optional second part: start with `+retaildev 0;set LTLOQSNSOT 7`, host a private lobby, press Start Match, and
count the countdown. 12 seconds (7 + 5) means the semicolon side door works. 8 seconds means it does not.

**T3. Timing of the step 1 build.**
Game closed, map open, F5. Read the F5 lines with their times. Pass: no fixed wait longer than about 2 s between
"signed in" and "selecting Multiplayer", between "Multiplayer menu" and "hosting", and none between "lobby" and
"starting". Write down the time of each line: this is the first DW client timeline. If hosting fails when sent
early, raise the settle value until it passes and record the value.

**T4. Splash skip (optional).**
In the console at the menu: `set NPLRKNKKOP 1` and `set LKSQOLNKLP 1`, quit, start again. Pass: the splash and boot
flow screens are shorter or gone. If nothing changes, the values are not saved or not read at boot; then only a
client side default can do it.

**T5. Real devmap (after step 4 is built).**
At the Multiplayer menu, console: `set MOLPOSLOMO war`, then `devmap <id>`. Pass: the map loads with no lobby and
you spawn. Watch for a freeze: the function loops until the NETWORKING fence passes. If it freezes for more than
20 s, close the game and drop this path. Then try it once on a stock map (`devmap mp_hackney_am`) to tell a custom
map problem from a devmap problem.

**T6. The full cold start (after step 3).**
Game closed, F5. Pass: no mode select screen, no menu clicks, `DW_DEVMAP` lines in `replay-dw-client.log` in the
order waiting, hosting, start requested, then the editor reports "In game on <id>". Compare the total time with T3.

---

## 6. What could not be determined

- U1. Settled in code, still worth the two minute run: the whitelist and its blanking store are in the recompiled
  exe (see "Which exe the addresses belong to"). Test T2 confirms the behaviour.
- U2. Whether `xstartprivatematch` from the bare Multiplayer menu gives a lobby that `xpartygo` accepts (doc 65 U1).
  Test T1.
- U3. Whether F5 as built has ever completed in game. The logs show deploys and `map_reload` today, not which path
  started the match.
- U4. The earliest frame at which the mode switch and the lobby host are safe. The code shows no gate; the safe
  conditions in step 8 are a judgment. Tests T3 and T6.
- U5. A timed boot of the DW client. Only the stock exe has a timed log, from 2026-10-04 with diagnostics on.
- U6. Whether `SV_DevMap_f` still works in a ship build. It is present in the recompiled exe (0x1413A6BB0 in
  `[DWMAP]`, vtable slot at 0x14251E188). Test T5.
- U12. How far the game gets when the online-services host is not running (menu, or stuck at sign-in).
- U13. Most IDA readings were not repeated on the recompiled exe (only the items listed near the top were).
- U7. Script side pre-match timers for `war` and their dvar tokens. Not in the engine's name table.
- U8. The effect of the splash and boot flow dvars on the DW client. Test T4.
- U9. Whether other code reads the raw command line besides `Com_ParseCommandLine`. Not searched exhaustively.
- U10. The stale request in `replay-dw-test.command` (it holds `... quit` today). `[DWLOG]` shows a
  `DW_LOCAL_TEST_COMMAND ... dispatched` line at every start, so the client runs that stale line on its first
  frame, and the game keeps running. Why `quit` does nothing there was not looked at. If the editor ever uses this
  file channel, it must delete the file before launch, as `launch-native-pair.ps1:107-109` does.
- U11. Whether a rebuilt package with a new id needs anything beyond the boot scan. For a cold start it does not:
  the scan runs at every boot.
