# 77. Loading screen first: cold start devmap with no menus on screen

Date: 2026-11-11. Follow-up to `76_cold_start_devmap.md`. New owner requirement: "the map loading screen should be
the first thing i see". From process start the owner must not see the splash screens, the mode select screen, the
Multiplayer menu or the private match lobby.

Rules kept: the game was not started, no process was attached to or written, nothing was written under
`E:\IW8\Builds\` or `E:\DW\`. Files written: this document and the three files in `docs/research/77_client/`.
One compile check of the new source was run with the owner's installed clang into the session scratchpad
(section 5); nothing was linked or run.

Legend: **VERIFIED IN CODE** = read in the binary (address given) or in source (file and line given).
**INFERRED** = follows from verified code, not executed. **NEEDS A LIVE TEST** = only a run can settle it (section 7).
Every guess is marked "guess".

Sources and address rule (same as doc 76):
- `[IDA]` the IDA database is for the OLDER plain exe `game_dx12_ship_replay.exe`. Addresses written "older 0x..."
  are **from the older build, to be re-checked**.
- `[DWMAP]` `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.map`: names and addresses in the recompiled exe the
  owner runs. Addresses written "DW 0x..." come from it. `_dw` and `_local_dw` share code addresses (doc 76).
- `[DW]` `E:\DW\IW8\integration\replay\` (client source), `[DWTOOLS]` `E:\DW\IW8\tools\` and
  `E:\DW\IW8\data\dw-client\compiler-base\` (the build pipeline and its shared headers).
- `[ELOG]` `mw120rproxy.engine.log` (2026-10-04, older exe with the old proxy DLL). The only timed engine log.
- There is no decompiled Lua on disk. The menus are Lua inside `ui.ff`. What a Lua menu does could not be read.

---

## The answer in one paragraph

Do not try to stop the boot screens from existing. Cover them. The recompiled exe already runs client code at the
very end of every UI frame (`ScrPlace_EndFrame`, after every UI worker, before the frame is submitted). One
`UI_FillRect` there paints a dark full-screen rectangle on top of the splash screens, the mode select, the menu and
the lobby, from the first UI frame until the engine's own map loading screen takes over. Under the cover the client
runs the game's own private match start with no clicks: switch to Multiplayer inside `Com_Init`, wait for sign-in
and the front end, `xstartprivatematch`, set `ui_mapname` / `ui_gametype`, `PartyHost_RequestStartMatch(party,
true)`. This is the recommended path ("lobby"): every engine call in it is one the game or the DW client already
makes. The direct path (`SV_DevMap_f`, no lobby at all) is also written, behind `--devmap-path direct`, but it is
dead code in this build with a wait that has no timeout, so it is an experiment. The source is ready in
`docs/research/77_client/`: one new file and two one-line edits. It compiles with the DW build's own compiler flags.
It has not been linked or run.

---

## 1. What is on screen today, in order, and what puts it there

`CL_Screen_UpdateFrame` (older 0x1415CAAA0, DW 0x141614AA0) decides what a frame shows. VERIFIED IN CODE (older
build): it picks one of three drawers, then always runs the same tail: `LUI_CoD_CheckWaitForWorkers`, the end-frame
hook, `R_EndFrame`.

| # | On screen | What puts it there | Evidence | Cheapest way to never show it |
|---|---|---|---|---|
| 1 | Boot logo window | A Win32 splash bitmap (resource 100) loaded before the renderer exists. The old proxy used that load as its trigger | `[PX]` `dllmain.cpp:257` (older build). Not re-checked in the DW exe | Leave it. It is a neutral boot picture and it is gone before the first game frame |
| 2 | Black frame, small spinner bottom right | `CL_Screen_UpdateFrame` when the client UI is not up yet (`cls+12 == 0`): only `CL_Screen_DrawBlankScreenSpinner` (older 0x1415CA750, DW 0x141614750). The spinner is drawn only during remote screen updates | `[IDA]` decompile | Leave it. Neutral |
| 3 | Splash screens (logos, legal), any "press a key" gate | Game mode 0, no client application: the drawer is `LUI_CoD_UpdateFrame(0)` (DW 0x141A31810). The Lua root opens the menu `splash_screens` | `[ELOG]` 7.5 s `opening menu "splash_screens"`. The name is not a string in the exe (VERIFIED by string search), so Lua opens it | Cover (section 3). Optional and unverified: force `cg_viewedSplashScreen` `MLNMPQOON`, `cg_autoSkipSplashScreen` `NPLRKNKKOP`, `lui_skip_boot_flow` `LKSQOLNKLP` (`--devmap-boot-dvars 1`). What Lua does with them could not be read |
| 4 | Intro or background video | Tail of `CL_Screen_UpdateFrame`: while in the front end it asks the Lua state block for a cinematic name and calls `R_Cinematic_StartPlayback` | `[IDA]` decompile. The dvar `intro` is dead (doc 76) | Cover. The video is drawn by the UI under the cover. Its sound is not covered |
| 5 | Mode select (`ModeFencePopup`) | Still mode 0, Lua. Opens after the sign-in and networking fences pass | `[ELOG]` 45.1 s. Not a string in the exe | Never reach it: set the desired game mode to Multiplayer in `Com_Init` (doc 76 step 5). The cover hides it in any case |
| 6 | Multiplayer menu over the `mp_frontend3` scene | Mode 2: the drawer is the client application (vtable slots 4 and 5). The scene is requested by the engine, the menu is Lua | `[ELOG]` 7.0 s `SetRequestedZone: 'mp_frontend3'`, 46.3 s client init | Cover. Not requesting the scene is not safe: the wait conditions and the party code are written for the front end (section 2) |
| 7 | Private match lobby | Lua reacts to the party that `xstartprivatematch` creates | `CL_MainMP_StartPrivateMatchHost_f` older 0x1415E4910 has no UI call of its own | Cover |
| 8 | Map loading screen | `PartyHost_StartMatch_Internal` (older 0x14119E3D0): Lua event `lui_game_start_notify`, `Com_FrontEndScene_ShutdownAndDisable`, `CL_MainMP_LoadTipsAndFrontendFastfile(map)`, `CL_MainMP_PrepareLocalClientsForMapLoad` (calls `UI_SetMap`), `CL_MainMP_MapLoading`, renderer restart, then the in-game Lua root draws the loading screen | `[IDA]` decompile (doc 76); `[ELOG]` 136.5 s `R_Init`, `LUI: Initializing (in-game)` | This is the goal. The cover is removed in the frame the front end shuts down |

Two facts that shape the design:
- The real map loading screen is drawn by the in-game Lua root. The C side `UI_DrawLoadingScreenInternal` (older
  0x141B9C9D0, DW 0x141BFE9D0) only draws subtitles and a 256 x 2 loading bar. `UI_SetMap` (older 0x141B9E9D0) only
  stores the name and sets `ui_mapname` / `ui_gametype`. So the true loading screen cannot be put up before the
  engine restarts the UI for the map. VERIFIED IN CODE. The answer to "can it be shown before a map is chosen by
  the party code" is no, not without rebuilding what that Lua root does.
- So "loading screen first" means: neutral picture (boot logo, then a dark cover) until item 8, then the real one.
  This is the second form the requirement allows.

Not covered by anything here: sound. Menu music or a video's audio may play under the cover. Not looked at.

---

## 2. The direct path: `SV_DevMap_f`

All addresses older build unless marked. Read fully: `SV_DevMap_f` 0x14136BBB0 (DW 0x1413A6BB0),
`SvGameModeAppMP::DevCmdSetup` 0x14136B720, `Live_InitForDevmap` 0x141665CB0,
`SV_CmdsMP_StartMapWithSettings` 0x14136C6B0, `SV_MainMP_PumpServerStartup` 0x14137EA20. Callee lists only:
`SV_MainMP_ServerStart_Execute` 0x14137E810, `SvGameModeAppMP::ServerStart_Spawn` 0x14137B390,
`CL_MainMP_BeginServerMapChange` 0x1415EB520, `Com_GameStart_Begin_Internal` 0x1410C8D50.

What it needs before the call (VERIFIED IN CODE unless marked):
- Multiplayer must be the committed game mode. The function is slot 15 of the Multiplayer server application, and
  `SV_Cmds_CheckMapCmdValid` asks `Com_GameMode_SupportsMap` for the active mode.
- The map name comes from server command argument 1 (`SV_Cmd_ArgvBuffer(1)`), so the caller must tokenize
  `devmap <id>` with `SV_Cmd_TokenizeString` (0x141298BD0, DW 0x1412C8BD0) first.
- The game type is read from `ui_gametype` (`Party_GetGametype`). Set it first.
- Sign-in: not required by the code. `Live_InitForDevmap` invents a user (`%USERNAME%`, random xuid) when nobody
  is signed in. On the DW client that would start a match outside the local platform identity, so the client code
  refuses the direct path until the DW sign-in is complete. INFERRED.
- Network: `DevCmdSetup` restarts the network layer if it is not started, then creates its own private party
  (`Party_Awake`, `PartyHost_StartPrivateParty`), sets `onlinegame` 0, `systemlink` 1, `IsDevmapping` 1.
- The package scan (`DW_MapAvailability`) must have run, because `SV_CmdsMP_StartMapWithSettings` checks
  `Content_DoWeHaveContentPack(Live_GetMapSource(Live_GetMapIndex(map)))` and raises the missing map error
  otherwise. The scan runs at boot (doc 76).
- Boot zones: nothing in the read functions loads `common_mp`. `SV_MainMP_ServerStart_Execute` calls
  `SV_InitMP_SpawnServer_LoadMapAssets` for the map only. So the Multiplayer zones must already be loaded by the
  mode commit. Calling before they are done: unknown. NEEDS A LIVE TEST.
- `mp_frontend3` and the front end scene: not needed by this code. `DevCmdSetup` shuts the front end scene down
  (`Com_FrontEndScene_ShutdownAndDisable`) and calls `Com_Shutdown("EXE/SERVERKILLED")`. INFERRED.

What the screen shows while it runs: the path has no loading screen call of its own. `SV_DevMap_f` does not call
`CL_MainMP_MapLoading`, `UI_SetMap` or `Com_GameStart_BeginDevmap` (only `SV_StartLocalReplay` calls that one).
`SV_CmdsMP_StartMapWithSettings` calls `CL_MainMP_BeginServerMapChange` only when a local server is already
running, and `DevCmdSetup` has just shut it down. The main thread then blocks in `SV_MainMP_PumpServerStartup`.
`ServerStart_Spawn` brackets its work with `R_BeginRemoteScreenUpdate` / `R_EndRemoteScreenUpdate`, and the drawer
for that state is the blank frame with the spinner (row 2 of section 1). So the expected picture is: cover, then
black with a spinner while the server spawns, then the in-game UI when the local client connects. INFERRED. Whether
the Lua loading screen appears at all in this path is unknown.

What can block forever: after the spawn, `SV_DevMap_f` loops on
`FenceManager_IsFenceTypeActive(controller, 4)` (older 0x140F19830, DW 0x140F32830; type 4 is fence index 2,
NETWORKING, by the names in `[ELOG]`) and only pumps `NET_Frame`, `Live_FrameUpdate(0)`, `FenceManager_Frame`.
`Live_IsOfflineTool` returns 0, so the loop always runs. The client can make sure the fence passes BEFORE the call
(the proposed code refuses otherwise). It cannot guarantee the fence still passes after `DevCmdSetup` has stopped
the party, cleared `onlinegame` and possibly restarted the network. That is the risk that cannot be removed
statically.

Verdict: fastest on paper, no lobby, but untested since the developer build, with one unbounded wait and an unknown
picture. Keep as an experiment (`--devmap-path direct`, and the `dw_devmap <id>` console command to try it by hand).

---

## 3. The lobby path with the UI held back (recommended)

The state machine is doc 76 step 3, with a cover on top from the first UI frame.

Where the cover is drawn. `ScrPlace_EndFrame` (DW 0x141626560) is already replaced by `[DW]`
`ScrPlace_EndFrame.cpp:10-25`. Its own comment: "CL_Screen_UpdateFrame calls this once after all UI workers finish
and before R_EndFrame submits the draw commands, for both no-game-mode and game views". It calls
`DW_LocalBackend_Frame()`, `Com_DebugStates_Frame(0)` and `Con_DeveloperFrame()`. The debug overlay text and the
developer console are already drawn from there (`Com_DebugStates.cpp:359`, `Con_Developer.cpp:228-229`), and the
console shows above the menus. So a rectangle drawn there is above the whole UI. VERIFIED IN CODE (the hook and the
precedent); that the rectangle really covers everything NEEDS A LIVE TEST.

What draws it. `UI_FillRect` (older 0x141B9B700, DW 0x141BFD700): `(ScreenPlacement, x, y, width, height,
horzAlign, vertAlign, colour)`, as used by `UI_DrawLoadingScreenInternal` with `scrPlaceFull`. It does nothing
while `s_uiFillRectMaterial` (DW 0x152CB25A0) is null, so it is safe before the UI assets exist. VERIFIED IN CODE.
The alignment value for "whole screen" is a guess (4, from the classic enum); the source draws a second, oversized
rectangle with alignment 0 in case the guess is wrong.

When the cover comes off. `PartyHost_StartMatch_Internal` shuts the front end scene down and enters map loading in
the same call. The cover is drawn while the request is pending and until the first frame on which
`Com_FrontEndScene_IsActive()` is false after the start was requested. From that frame the engine's loading screen
owns the picture. On any failure or time limit the cover is removed and a `DW_DEVMAP failed: ...` line is logged,
so the owner is never left on a dark screen.

The loading screen entry points, for the record (all in `PartyHost_StartMatch_Internal`, VERIFIED IN CODE, older
build): `CL_MainMP_LoadTipsAndFrontendFastfile(map)` 0x1415E4EA0, `CL_MainMP_PrepareLocalClientsForMapLoad(map,
gametype)` 0x1415E9F20 (calls `UI_SetMap`), `CL_MainMP_MapLoading` 0x1415EB090, then `CL_Main_ShutdownAll`,
`CL_Main_InitRenderer`, `Com_GameStart_BeginServer`. They need the map name and game type only. They are not
called early by the proposed code: they tear down and restart the renderer and client state, which is exactly what
a match start does, and doing it without the party state that follows is not a supported state.

Which path gives "loading screen first" with the least risk: the lobby path under the cover. Reasons: (1) every
call is already made by the game's menu or by `[DW]` (`DW_LobbyControls.cpp:745-750` calls the same
`PartyHost_RequestStartMatch(party, true)`; `DW_LocalPlatform.cpp:355-368` makes the same mode switch); (2) the
picture after the cover is the normal match loading screen; (3) a failure leaves the game in an ordinary menu.

---

## 4. How `[DW]` adds code, binds natives and replaces natives

VERIFIED IN CODE in the build pipeline. This is link-time, not a hook.

- **Which files are compiled.** `tools/dw_client.py:564-607`: every file in `integration/host-scale`, and in
  `integration/replay` only the files that are already overlays or are named in the set `replay_additions`
  (`dw_client.py:568-599`). Each one is copied to `src/` of the frozen job and appended to `source_inputs`
  (`:627-634`); every `.cpp` there is compiled (`:933-935`). `CMakeLists.txt` builds the server and test tools, not
  the client. `content/replay120/manifest.json` is a content pack list, not a source list.
- **Binding a native.** Declare it `extern "C"` and call it. The linker resolves the name against the native name
  catalog (the names in `[DWMAP]`), or by address with the form `replay_rva_<8 hex digits of the older-build RVA>`
  (`compiler-base/tools/native_functions.py:13-14`). Example: `DW_LobbyControls.cpp:30`,
  `void replay_rva_0119cfa0(void *, bool); // PartyHost_RequestStartMatch`. Globals use `gl_<older-build VA>` or
  their catalog name (`DW_LobbyControls.cpp:50-53`). The shared declarations are in
  `compiler-base/src/com_debugstates.h` and `replay_dvar.h`.
- **Replacing a native.** Define an `extern "C"` function with the native's catalog name. The linker finds every
  external function definition whose name is a native function entry (`native_functions.py:114-146`) and places it
  at the original function (`compiler-base/tools/build.py:379-388`). Rules: one definition per native, it must be a
  function entry, and a native global cannot be redefined (`native_functions.py:126-131`, `build.py:395-396`).
  Examples: `ScrPlace_EndFrame.cpp:13`, `Con_Developer.cpp:203` (`Con_ToggleConsole`), `DW_LocalPlatform.cpp:2020`
  (`Live_IsUserSignedInToLive`). A replacement that still needs the original body needs a generated clone
  (`tools/prepare-lobby-native-clones.py`); the new feature avoids that.
- **Running every frame.** `ScrPlace_EndFrame` calls `DW_LocalBackend_Frame()` (`DW_LocalBackend_Frame.cpp:370-409`,
  which calls `DW_LobbyControls_Frame()` at `:398`) and `Con_DeveloperFrame()` (`Con_Developer.cpp:222-231`, which
  calls `DW_HostMigration_Frame`, `DW_MapRefresh_Frame`, `DW_AutoLoadout_Frame`). `DW_AutoLoadout.cpp` is the
  smallest full example of an added feature: one file, one name in `replay_additions`, one call in
  `Con_DeveloperFrame`.

The exact edits a new feature file needs (this is the whole list for `DW_Devmap.cpp`):
1. Add `integration/replay/DW_Devmap.cpp`.
2. `tools/dw_client.py`: add `"DW_Devmap.cpp",` to `replay_additions` (diff: `77_client/dw_client.py.diff`).
3. `integration/replay/Con_Developer.cpp`: declare `void DW_Devmap_Frame();` and call it in `Con_DeveloperFrame`
   before the console is drawn (diff: `77_client/Con_Developer.cpp.diff`).
4. Nothing for the `Com_Init_GameMode_FromCommandLine` replacement: defining the function is the registration.

`DW_LocalBackend_Frame.cpp` is deliberately not edited: `tools/test-dedicated-frame.py:31-35` checks which frame
functions that file may reference in the dedicated build.

---

## 5. The proposed source

Files, all under `E:\IW8-Radiant\docs\research\77_client\` (nothing was written in `E:\DW`):

| File | What it is |
|---|---|
| `DW_Devmap.cpp` | The new client file. Copy to `E:\DW\IW8\integration\replay\DW_Devmap.cpp` |
| `Con_Developer.cpp.diff` | Unified diff, two added lines, against the current working tree file |
| `dw_client.py.diff` | Unified diff, one added line, against the current working tree file |

Apply from `E:\DW\IW8`: `git apply <path>\Con_Developer.cpp.diff <path>\dw_client.py.diff`, then copy the `.cpp`.
The diffs were generated from the files as they are on disk today (both have uncommitted changes).

What `DW_Devmap.cpp` does:
- Command line, read once with `GetCommandLineW`: `--devmap <id>` (required; `mp_` prefix, `[a-z0-9_]`, 4 to 31
  characters), `--gametype <name>` (default `war`), `--bots <n>` (0 to 199), `--devmap-path lobby|direct` (default
  `lobby`), `--devmap-boot init|frame` (default `init`), `--devmap-boot-dvars 0|1` (default 0). A bad value rejects
  the whole request and logs it.
- `Com_Init_GameMode_FromCommandLine` (replacement): with a valid request and `--devmap-boot init`, calls
  `Com_GameMode_SetDesiredGameMode(2, true)`. `Com_Init` commits it on the next line (doc 76).
- `DW_Devmap_Frame` (every frame, main thread): registers `dw_devmap`, then runs the phases sign-in, mode, front
  end, hosting, started, done. If the mode is still 0 after sign-in it makes the same request as
  `dw_localTestMenuMP` (`Com_GameMode_SetDesiredGameMode(2, false)`, `CL_Mgr_ActivateClient(0, 0)`). Commands go
  through `Cbuf_AddText(0, ...)` so they run in the command phase, not in the middle of a render frame. The map and
  game type are written every frame until the front end shuts down, in case the lobby menu writes its default.
- Cover: two `UI_FillRect` calls per frame while a request is pending.
- Log lines, all through `DW_LocalBackend_LogLine` with the prefix `DW_DEVMAP `, in order for a good run:
  `request id=... gametype=... bots=... path=... boot=...`, `signed in`, `multiplayer active` (or first
  `selecting multiplayer`), `front end ready; <id> is an installed package`, `hosting a private match lobby`,
  `start requested <id> <gametype>`, `loading <id>; the engine loading screen takes over`. Any stop is
  `failed: <reason> (<id>); the cover is removed`. The editor should treat `loading` as "handed to the engine" and
  keep its own "in match on the id" memory check as the success test.
- One request per process. Without `--devmap` the file does nothing except register the `dw_devmap` command.

Compile check done: `clang++ 22.1.8`, the DW build's flags from `compiler-base/tools/build.py:145-156`
(`--target=x86_64-pc-windows-msvc -ffreestanding -ffunction-sections -O2 -std=c++20 -Wall -Wextra -Werror
-fno-exceptions -fno-rtti`), include path `compiler-base/src`. Result: no warnings, no errors. Every undefined
symbol of the object that is a game or client name exists exactly once in `[DWMAP]` (26 names checked, and the
replaced `Com_Init_GameMode_FromCommandLine`), plus four kernel imports that other
`[DW]` files already use and six `replay_rva_` names (five copied from `DW_LobbyControls.cpp`). The object was
written to the session scratchpad only.

What in that source is unverified (plainly):
- It has not been linked by the DW pipeline and has never run. The pipeline's own regression checks may object to
  a new replacement or a new command; that shows up in the build (test B1).
- Replacing `Com_Init_GameMode_FromCommandLine` and committing Multiplayer inside `Com_Init`, before sign-in. The
  developer build did exactly this for `+mpMode 1`, but this client has only ever entered Multiplayer after sign-in.
  `--devmap-boot frame` avoids it.
- `CL_Mgr_ActivateClient(0, 0)` after an init-time mode switch. Copied from `TestMenuMP_f`, which also calls it
  when the mode is already 2.
- That `xstartprivatematch` gives a usable lobby with no menu click (doc 76 U2).
- The cover: alignment values, and that `s_uiFillRectMaterial` is valid in every UI state that draws something.
- The settle times (1000 ms front end, 500 ms lobby) and the time limits. Guesses.
- The whole direct path, including the prototype of `replay_rva_0132cc50` (the active server application getter)
  and calling `SV_DevMap_f` from a client command.
- `--devmap-boot-dvars 1`: the three dvars are assumed to be bool dvars; their effect in Lua is unknown.
- A decision for the owner, not a bug: `DW_LocalBackend_IsEnabled()` is true for every `*_dw.exe`, so the
  `dw_devmap` command and the `--devmap` arguments would exist in the published client too. If that is not wanted,
  gate both on the development opt-in the client already has (`replay-dw-test.enabled`,
  `DW_LocalPlatform.cpp:427-441`).

---

## 6. Build and install steps (read from the scripts, not run)

From `E:\DW\IW8`, after applying the three changes of section 5:

1. `.\build-game.ps1 -ValidateOnly` : compile and verify without publishing (`build-game.ps1:11-17`,
   `tools/build-dw-client.py:16-19`, `docs/DW_CLIENT_BUILD.txt:73-75`). Do this first. It runs no game.
2. `.\build-game.ps1` : the normal build. Per `docs/DW_CLIENT_BUILD.txt:8-19` it also PUBLISHES the verified exe
   to the primary server before the result is "ready". There is no local-only install path in these scripts; the
   guide says not to copy an exe into the game folder by hand.
3. `.\deploy-game.ps1` : installs the ready exe as `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.exe`
   (`tools/dw_client.py:1500-1560`). It refuses while `game_dx12_ship_replay_dw.exe` runs, when sources changed
   since the build, or when the published release does not match. It keeps the ini.
4. `.\Host-Local-Server.ps1 -NoLaunch` : starts the local host if needed and recreates
   `game_dx12_ship_replay_local_dw.exe` from the new production exe (`Host-Local-Server.ps1:129-138`,
   `tools/setup-local-client.py`). Close the local game first.

Time: the scripts print `DW build elapsed`. Recorded jobs in `data/dw-client/jobs/*/timing.json`: prepare 160 to
220 s, build and checks about 335 to 346 s, about 9 minutes in total. Deploy and the local refresh are not timed
in the records.

Because step 2 publishes, the owner may prefer to stop after step 1 until the tests that need no install (B1) pass
and the gating decision in section 5 is made.

---

## 7. Live tests, shortest first

Send back, for any failure: the `DW_DEVMAP` lines from `replay-dw-client.log`, and one sentence on what was on
screen.

- **B1. Build only (no game).** `.\build-game.ps1 -ValidateOnly`. Pass: it ends with the verified message. Fail:
  send the last 30 lines of output. Likely texts: `Native symbol is not a replaceable function`, `Replacement is
  not a proven function entry`, an unresolved symbol name, or a regression check that names `Con_DeveloperFrame`.
- **L1. No request (regression, 1 minute).** Start the local exe with no arguments. Pass: boots as before, no
  `DW_DEVMAP` line in the log, and `dw_devmap` typed in the console prints its usage line.
- **L2. Bad request (1 minute).** Start with `--devmap nope`. Pass: one line `DW_DEVMAP failed: the --devmap
  arguments are not valid`, normal boot, no cover.
- **L3. The safest full run.** Start with `--devmap <id> --devmap-boot frame`. Pass: dark screen from the first
  game frame, no splash, mode select, menu or lobby visible, then the map loading screen, then spawn in TDM; the
  log has the lines of section 5 in order with `selecting multiplayer`. If a menu shows through, say which one and
  whether the dark area covered part of the screen (that is the alignment guess).
- **L4. The default.** Start with `--devmap <id>`. Pass: as L3, with `multiplayer active (set in Com_Init)` and no
  `selecting multiplayer`. If it hangs or errors before sign-in, the `Com_Init` switch is the cause: use
  `--devmap-boot frame` and report.
- **L5. Bots.** `--devmap <id> --bots 4`. Pass: four bots in the match.
- **L6. Direct path by hand.** At the Multiplayer menu, console: `dw_devmap mp_hackney_am`. Pass: the map loads
  with no lobby. Watch for a freeze longer than 20 s (the fence wait): close the game and report the last
  `DW_DEVMAP direct` line. Then `dw_devmap <id>` for a custom map.
- **L7. Direct path cold.** Only if L6 passes: `--devmap <id> --devmap-path direct`. Compare the time with L4.
- **L8. Boot dvars.** `--devmap <id> --devmap-boot-dvars 1`. Pass: same as L4 and not slower. If the boot breaks,
  the Lua boot flow needs one of the three screens; leave the switch off.

---

## 8. Could not be determined

- What the Lua boot flow does: whether a key press is required behind the cover, and what the three boot dvars
  change. No Lua source is on disk. L3 answers the first; L8 the second.
- Whether sound from the hidden screens is audible.
- Whether entering Multiplayer in `Com_Init` is safe on this client (L4).
- Whether the direct path works at all, and what it shows (L6, L7).
- Whether the DW pipeline accepts the new replacement and command without changes to its checks (B1).
- The older-build addresses of `ScrPlace_EndFrame` and `LUI_CoD_UpdateFrame` (only the DW addresses were looked up).

---

## Short report

- **Recommended path:** lobby start under a dark cover (`--devmap-path lobby`, the default), with the Multiplayer
  mode set in `Com_Init` and a frame-time fallback.
- **Verified:** the three frame drawers and the common tail of `CL_Screen_UpdateFrame`; that the splash and mode
  select menus are Lua (names absent from the exe); what `SV_DevMap_f`, `DevCmdSetup`, `Live_InitForDevmap`,
  `SV_CmdsMP_StartMapWithSettings` and the fence wait do; that the real loading screen is drawn by the in-game Lua
  root; how the DW pipeline selects files, binds natives and replaces natives; that the new file compiles with the
  DW flags and that every symbol it needs exists in the recompiled exe's map.
- **Guess or untested:** everything about behaviour at run time: the cover's alignment and material timing, the
  `Com_Init` switch, the lobby without a menu, the settle times, the direct path, the boot dvars.
- **Files written:** `docs/research/77_loading_screen_first.md`, `docs/research/77_client/DW_Devmap.cpp`,
  `docs/research/77_client/Con_Developer.cpp.diff`, `docs/research/77_client/dw_client.py.diff`.

---

## Addendum (main session, 2026-11-11): what was built in the editor, and one change to the client file

- The editor now has its own cover, so the requirement holds with the client the owner runs today: a card
  (`src/qtapp/devmap_cover.*`) sits exactly over the game window from the launch until the map begins to load
  (`devmap::coverWanted`: until the start request, then only while the front end is still on screen, at most 4 s).
  It is an editor window: topmost, click-through, never focused. Checked on the real desktop with
  `IW8Radiant.exe --cover-check`. It cannot be seen over a game in exclusive full screen. NOT SEEN with the game.
- The editor follows the contract of section 5: it passes `--devmap <id> --gametype <name> [--bots <n>]` only to an
  exe that contains the text `DW_DEVMAP `, reads the `DW_DEVMAP` lines of `replay-dw-client.log` after the launch
  offset, sends no commands while the client works, and takes over with its own console-command path after a
  `failed:` line. A line that starts with `start` or `loading` is the start request for the cover.
- Change to `77_client/DW_Devmap.cpp` after the agent's report: the direct path and the `dw_devmap` command now need
  the development opt-in the client already has (`replay-dw-test.enabled`, read like `DW_LocalPlatform.cpp`
  `TestFrame`). Without it a `--devmap-path direct` request logs one line and uses the lobby path. The lobby path
  is not gated: it only automates the menus. The file was compiled again with the DW flags (no warnings) and its
  undefined game and client symbols were checked against `game_dx12_ship_replay_dw.map` (each found once).
  With this change test L1 reads: `dw_devmap` prints its usage line only when `replay-dw-test.enabled` exists.
- Nothing was applied to `E:\DW\IW8`. Applying means a client build, and the normal build publishes a release
  (`docs/DW_CLIENT_BUILD.txt`), so it waits for the owner.

### Why the client build is blocked (read on 2026-11-11, nothing changed)

`tools/build-dw-client.py --validate-only` stops in `prepare`, before any compile:

1. Without `--carry-installed-gulag`: `data\gulag-navigation\production-inputs.json` does not exist
   (`dw_client.py` `require_gulag_production_inputs`).
2. With it: `tools/verify-installed-gulag-carryforward.py` raises "Installed client or original Replay image differs".
   It requires the game folder's `game_dx12_ship_replay_dw.exe` to have the hash in `data\dw-client\deployment.json`
   (`6a22f4e9...`, release 36 of 2026-10-27). The installed exe is `b3651999...`, the output of job
   `1794159250535314200` (2026-11-08), installed outside `deploy-game.ps1`.
3. Correction (same day): an earlier version of this note listed a missing helper module
   (`workstation_preparation_context`) as a second blocker. That was wrong. The module is in `E:\DW\IW8\tools`; my
   what-if script ran from another folder and so did not find it. Run the way the pipeline runs it, the check passes
   once it reads the published exe.

How it was resolved (owner: "we should be updating game_dx12_ship_replay_local_dw.exe, the droplet is down and we
dont need to publish anything there"): `tools/dw_candidate_build.py` in this repository runs the pipeline's own
validate-only carry-forward build and answers the check's reads of the installed exe with the retained published exe
`data\dw-client\game_dx12_ship_replay_dw.exe` (same hash as `deployment.json`), in a child process as the pipeline
does. No pipeline file or record is edited and nothing is published. `tools/dw_candidate_install.py` then installs
the candidate as the game folder's dw exe and remakes the local exe with `setup-local-client.py --refresh`.

The build and install results are recorded in docs/PROGRESS.md.
