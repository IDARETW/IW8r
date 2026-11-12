# 25. Live link for the IW8 Radiant Suite (roadmap L7)

Date: 2026-11-08. Scope: what the editor can drive in a running Replay 1.20 match today, through the two channels that already exist, and what needs a proxy change.

Read-only research. Nothing was started, stopped or injected. Nothing was written under `E:\IW8\Builds\1.20-replay`. No repo was edited, built or committed. The only file written is this report.

Legend: **VERIFIED** = read in source or in a file. **INFERRED** = derived from source, not run in game. **UNKNOWN** = not established.

Shorthands:
- `[PX]` = `E:\mw124\mw120rproxy\mw120rproxy\` (proxy source).
- `[LV]` = `E:\mw124\mw120rproxy\tools\live\live.py` (DW channel client).
- `[DW]` = `E:\DW\IW8\integration\replay\` (DW client source, the reader of the DW channel).
- `[RPL]` = `E:\IW8\Builds\1.20-replay\` (Replay install, read only).
- `[IW3]` = `E:\IW8-Radiant\reference\iw3xo-radiant\` (iw3xo reference).
- `[DOC19]` = `docs/research/19_replay_test_loop.md`, `[DOC05]` = `docs/research/05_runtime_evidence_1_20.md`.

---

## Summary table

| # | Question | Answer | Evidence |
|---|---|---|---|
| 1 | Is there a control protocol today? | Yes. File endpoint `[RPL]\.proxy\control.ini`, polled every 250 ms, answered by `.proxy\control-result.json`. Single slot, one request at a time, no socket. | `[PX]developer_ui.cpp:208,219,230-238,387-393` |
| 2 | Run console commands | **YES** (`action=command`). Fire-and-forget: `ok=true` means "queued in the native buffer", not "executed". Needs systemlink on. | `[PX]developer_ui.cpp:349-351,84-115` |
| 3 | Select a custom map | **YES** (`action=select`, lobby + systemlink). It rescans, then writes `ui_mapname`. It does not start the match. | `[PX]developer_ui.cpp:352-360,142-180` |
| 4 | Load / start the map | **NO** action. Start Match is a manual lobby step. `devmap`/`map` via the command path is UNKNOWN. | `[DOC19]` §2.7 (UNKNOWN); `[PX]developer_ui.cpp:170-172` |
| 5 | Refresh the package list | **YES, indirectly**: `select` rescans first. A standalone rescan (`hotkey f6` then `hotkey escape`) is possible but UNVERIFIED and risky. | `[PX]developer_ui.cpp:116-127,129-140,251-287`; `[PX]custom_maps.cpp:460-464` |
| 6 | Report the current map | **YES**. `status` returns `map` (ui_mapname), `selected` (active package id), `frontend`, `console`, `browser`. | `[PX]developer_ui.cpp:380-386` |
| 7 | Set dvars | **YES, fire-and-forget**, via `command set <name> <value>` (readable names translated). No generic readback. | `[PX]developer_ui.cpp:349-351`; `[PX]commandtext` (translate) |
| 8 | Read the player camera (game to editor) | **YES** in a match (`action=player`), returns origin and view angles as text in `data`. | `[PX]developer_ui.cpp:315-335` |
| 9 | Set the player camera (editor to game) | **PARTIAL / UNKNOWN**. The proxy handles `mw_setpos` (with optional yaw and pitch) and `noclip` in `noclip.cpp`; it has no `mw_setang`, which exists only in the DW build. Reaching the proxy handler with a `command` request is UNVERIFIED. | `[PX]noclip.cpp:30-108`; `[LV]` `SERVER_COMMANDS` line 44 |
| 10 | Entity hot update | **NO**. The control channel has no entity or geometry action. Needs rebuild + map change or a new proxy hook. | `[DOC05]` §12; `[DW]` `map_restart` message (`[RPL]` log) |
| 11 | DW command channel | Exists (`replay-dw-test.command`, `replay-dw-test.enabled`, `replay-dw-client.log`). Reader is the DW client build, not the proxy. Gate is latched at game launch. No nonce echo. | `[DW]DW_LocalPlatform.cpp:426-487`; `[LV]` lines 86-98 |
| 12 | Is there a socket / TCP live link in the proxy? | **NO**. The proxy comment states "No socket, arbitrary native calls, or writes to game memory are exposed." FEATURE_MATRIX section M claims otherwise (see §0). | `[PX]developer_ui.cpp:207-208`; no Winsock in `[PX]` |
| 13 | Proposed minimal protocol | JSON lines over `127.0.0.1` TCP, default port 3700 (same as iw3xo), behind an explicit proxy opt-in. Control file stays as the no-change fallback. | §5 |

---

## 0. Corrections to existing docs

- `docs/FEATURE_MATRIX.md` section M row "Editor ↔ game connection" says "Live-link server inside mw120rproxy (already injected into Replay 1.20)". **Not true of the proxy source.** The proxy has no socket code and states the opposite (`[PX]developer_ui.cpp:207-208`). Port 3700 belongs to iw3xo (`[IW3]src/components/modules/remote_net.cpp:93`), which is a reference, not this runtime. Update the row to "not built; control file exists".
- Section M row "Send console commands / dvars: `Cbuf_AddText`" is partly right. The proxy does not call `Cbuf_AddText`. It hooks it (`ConsoleText`, `[PX]developer_ui.cpp:557`) and writes into `s_cbufMainBuffer` through `Submit` (`[PX]developer_ui.cpp:84-115`). The control `command` action uses `Submit` directly (`:351`). Both reach the same buffer.
- `[DOC05]` §12 (name conflict): the proxy's `SetOrigin` and `SetViewAngles` names are still unverified. `noclip.cpp` already calls them for `mw_setpos` (`[PX]noclip.cpp:105-108`), so the behaviour is in use even though the names are not proven.

---

## 1. The control.ini protocol (proxy, today)

### 1.1 Transport and polling (VERIFIED)

- File: `<game>\.proxy\control.ini`. The game dir is the directory of the running EXE (`[PX]developer_ui.cpp:223-228`).
- Read with `GetPrivateProfileIntW`/`GetPrivateProfileStringW`, section `[request]` (`:230-238`).
- Polled from `DrawOverlay` (`:448-455`), on the main thread, for local client 0, with a 250 ms throttle (`:219-220`). `DrawOverlay` is the native overlay callback. Whether it runs during gameplay as well as the lobby is UNKNOWN; the `player` and `key` actions assume it does.
- Result: `<game>\.proxy\control-result.json`, written to `control-result.json.tmp` and then `MoveFileExW` with replace (`:387-393`).
- Each processed request logs one line: `[INFO][Control] request=%u action=%ls accepted=%d` (`:394`).

### 1.2 Keys in `[request]`

| Key | Type | Rule | Evidence |
|---|---|---|---|
| `pid` | uint | Must equal the proxy's `GetCurrentProcessId()`. Otherwise the request is ignored and no result is written. | `:230-231` |
| `id` | uint | Must be nonzero and differ from the last processed id (`static lastId`). A repeated id is ignored. The id is per process, so it resets on game restart. | `:232-234` |
| `action` | string, max 31 chars | Default `status`. Unknown action gives `ok=false`. | `:237,249-361` |
| `value` | string, max 4095 chars | If any character is outside 32..126 the value is cleared (`:239-247`). | `:238,240-247` |

Notes:
- INI is read by the Win32 API. Write the file as ASCII with CRLF or LF. Leading and trailing spaces are trimmed.
- The stale file in `[RPL]` (`control.ini`, Sep 30) has `pid=23504 id=16911 action=command value=com_frontEndAnimation normal`. `control-result.json` (Sep 29) has `pid=19976 id=6`. Neither matches each other. An editor must match both `pid` and `id` before trusting a result. Never reuse an old id.

### 1.3 Actions (VERIFIED from source; gates as coded)

| action | value | Gate | What it does | Evidence |
|---|---|---|---|---|
| `status` | (none) | none | Result only. | `:249-250` |
| `hotkey` | `f6` `f7` `grave` `enter` `escape` `up` `down` `backspace` | systemlink | Sends a down and up key event through the detoured handler. Allowlist only. | `:251-287` |
| `key` | `wasd` or `space` or `escape`, then a space and 50..5000 ms | not frontend, console closed, systemlink | Holds a key, released from the frame callback. Gameplay input. | `:288-314` |
| `player` | (none) | not frontend | Reads player origin and view angles (and entity origin and angles) into `data`. | `:315-335` |
| `console` | `open` `close` `toggle-output` | none | Opens or closes the in-game console. | `:336-345` |
| `browser` | `open` `close` | none | Opens or closes the custom map browser (F6). `open` calls `Refresh()`. | `:346-348`, `:129-140` |
| `command` | any printable text | systemlink, text nonempty | `Submit(commandtext::Translate(text) + "\n")`. Translates readable dvar names to tokens. Does NOT pass through `ConsoleText`, so `mw_maps` is not special here. | `:349-351`, `:557-586` |
| `select` | a package id (`[a-z0-9_]`, max 63) | frontend (lobby) and systemlink | `Refresh()` (rescan), then find the id, then `SelectPackage()`, which submits `set NSQLTTMRMP <id>` and `set MOLPOSLOMO war`. `ok` means the request was queued (`g_pendingFrames != 0`), not confirmed. | `:352-360`, `:142-180` |
| `read` | `<hex rva> <length>` | none, bounded | Reads up to 512 bytes at an RVA below `0x1324B000`, returns hex in `data`. Read only. | `:361-385` |
| other | | | `ok=false`. | |

`CanSelect()` = frontend AND systemlink (`:68-71`). `LPSPMQSNPQ` is the systemlink token (`:70`).

### 1.4 Result JSON fields (VERIFIED, `:380-386`)

```
{"pid":<int>,"id":<int>,"ok":<bool>,"frontend":<bool>,"map":"<ui_mapname or empty>",
 "selected":"<active custom package id or empty>","console":<bool>,"browser":<bool>,"data":"<text>"}
```

- `map` = token `NSQLTTMRMP` (`ui_mapname`), only if it passes `SafeName`.
- `selected` = `custommaps::Active()` (the package the proxy will load).
- `data` = `player_origin=x,y,z entity_origin=... view_angles=p,y,r entity_angles=...` for `player`; hex bytes for `read`; empty otherwise. Not JSON-escaped; the payloads contain only digits, commas and hex, so it is safe as written today.
- No field reports the command's output, its error text, or whether a command actually ran.

### 1.5 Asynchronous confirmation

- `select` sets a pending state for 300 overlay frames (`:185`, `:463-472`). `DrawOverlay` confirms when `ui_mapname` reads back the id ("native ui_mapname readback confirmed") or logs a failure after the frame count runs out (`[PX]developer_ui.cpp:460-472`). A client should poll `status` until `map` equals the id and `selected` is set.

### 1.6 Answers to question 1

- **Run console commands:** YES, `command`. Output is not returned; check the game log or the effect.
- **Select or load a custom map:** `select` YES (lobby only). Load/start: NO. The owner or a later lobby action starts the match. Starting a match from the control file is UNKNOWN and not implemented.
- **Refresh the package list:** `select` rescans as its first step (`:353`, then `custommaps::Refresh` via `Refresh()` at `:116-127`). `status` does not rescan.
- **Report the current map:** YES (`status` → `map`, `selected`).

---

## 2. The DW command channel (live.py)

### 2.1 Files (VERIFIED)

| File | Role | Evidence |
|---|---|---|
| `[RPL]replay-dw-test.command` | Request, single slot, one line: `<decimal nonce> <command>` | `[LV]:33,97-98`; `[DW]DW_LocalConfig.cpp:33` |
| `[RPL]replay-dw-test.enabled` | Gate, empty file. Read once by the game (see 2.3) | `[LV]:34`; `[DW]DW_LocalConfig.cpp:34`; `[DW]DW_LocalPlatform.cpp:436-437` |
| `[RPL]replay-dw-client.log` | Responses: dispatch and ack lines, plus each command's own output | `[LV]:35`; `[DW]DW_LocalPlatform.cpp:286` (log open) |

The file names can be overridden by the DW ini keys `test_command_file` and `test_enabled_file` (`[DW]DW_LocalConfig.cpp:219-221`), subject to the `replay-dw-test-` prefix and extension checks (`:115`, `:230-233`). Only the default names are used by `live.py`.

### 2.2 Request format and writer (VERIFIED)

- `live.py send()` (`[LV]:86-103`): checks that the game is running (`tasklist`), that `.enabled` exists, and that the command is printable ASCII, at most 460 chars, with no `;` (`:92`). Writes `"<time_ns> <command>"` as ASCII to `replay-dw-test.command.pending`, then `os.replace` onto the real name (`:96-98`). Atomic rename.
- `translate()` (`[LV]:56-66`): dvar names become tokens from `E:\DW\IW8\integration\replay\con_dvar_names.h`. Server commands (`noclip`, `mw_noclip`, `mw_setpos`, `mw_setang`, `mw_getpos`) get a `cmd ` prefix (`:44`, `:56-60`).

### 2.3 Reader (VERIFIED, in the DW client source, not the proxy)

- Reader: `TestFrame()` in `[DW]DW_LocalPlatform.cpp:426-488`, called once per frame from `:1988` on the main thread.
- Gate: on the first call, `replay-dw-test.enabled` is opened once. The mode is latched (`:436-437`). A file created after launch has no effect until the next launch. `live.py enable` says the same (`[LV]` docstring).
- Poll: every 500 ms (`:450`). Reads up to 511 bytes (`:459`).
- Dedupe: FNV digest of the whole content; identical content is ignored (`:466`). The nonce makes each request unique.
- Format check: leading digits, then one space, then at least one byte. Rejects control characters and `;` (`:467-475`).
- Dispatch: logs `DW_LOCAL_TEST_COMMAND native main-thread command dispatched` (`:477`), then `DW_LOCAL_TEST_CHANNEL replay-dw-test.command` (`:481-482`), then runs `Cmd_ExecuteSingleCommand_Internal(0, 0, text)` (`:487`). This is the native command parser, not `Cbuf`, and not the proxy.

### 2.4 Responses (VERIFIED)

- No response file. The acknowledgement is a log line. It does not echo the nonce, so two requests cannot be correlated from the log alone. The request file is single slot; a second write replaces the first.
- Results are whatever the command logs itself. Examples in the live log tail (`[RPL]replay-dw-client.log`): `map_refresh: techsets_mp_iw8_sample_r_refresh.ff ... needed beside the package`, `map_restart: native full restart (level zones stay loaded; package changes need a map change or map_refresh).`
- `live.py send()` waits up to `timeout` for the dispatch line, then for a `result` regex on later lines (`[LV]:101-116`). On a timeout it exits with an error.
- The reader's `DW_POS` format (position, read by `mw_getpos`) is used by `live.py pos/goto/look/view`.

### 2.5 Which build reads it

- The DW client build: `game_dx12_ship_replay_dw.exe` and `game_dx12_ship_replay_local_dw.exe` (`[LV]:36`). Not in the proxy's process name list (`[DOC19]` row 13).
- The stock proxy build (`game_dx12_ship_replay.exe`) does not contain this reader (no `replay-dw-test` in `[PX]`; grep confirmed).

### 2.6 Answers to question 2

- Names, format, reader, responses and the `.enabled` gate are as above.
- The `.enabled` gate is latched at launch. Creating the file needs a game restart. Creating it automatically would change how the owner's DW build starts, so it must stay a manual step.

---

## 3. What a minimal editor live link can do today (no proxy change)

### 3.1 Capability table

| Need | Today via control.ini | Notes |
|---|---|---|
| Detect that Replay runs and its pid | `tasklist` for `game_dx12_ship_replay*.exe` (editor already names `game_dx12_ship_replay.exe` at `E:\IW8-Radiant\src\qtapp\main_window.cpp:892`) | The pid must go into `[request] pid`. |
| Health check and current map | `action=status` | Result `map`, `selected`, `frontend`, `console`, `browser`. |
| Rescan packages and select the just-deployed map | `action=select value=<id>` in the lobby | Equivalent to F6, Enter. The editor should then `status` until `map == id`. |
| Start the match | NOT available | Manual "Start Match". UNKNOWN whether `command` can do it safely. |
| Run a console command | `action=command value=<text>` | Fire-and-forget. Allowlist the text (section 4). |
| Set a dvar | `action=command value=set <name> <value>` | No readback except tokens the proxy reads (`map`, `selected`). |
| Read the player camera (game to editor) | `action=player` in a match | Poll at 4 Hz or less. Gives origin and angles as text. |
| Move the player camera (editor to game) | `action=command value=cmd mw_setpos x y z yaw pitch` | UNVERIFIED on the proxy build. The `cmd ` prefix is inferred from the DW translator (`[LV]:44,56-60`). The handler is `[PX]noclip.cpp:30-108` (it also enables noclip and sets pm_type 2). Test on a scratch match first. |
| Entity or geometry hot update | NOT available | Needs a rebuild and map change, or a new hook. |

### 3.2 Minimal editor procedure

1. Find the game pid with `tasklist /fo csv /nh` (filter by the exe name).
2. Write `[request]` with `pid`, a new `id`, `action=status`, using a temp file and `MoveFileEx` in `.proxy\`. Wait up to 2 s for `control-result.json` with the same `pid` and `id`.
3. In the lobby: `action=select`, then `status` until `map` and `selected` show the id. Report "Close the browser, then Start Match" to the user (the proxy's own status text, `:433-434`).
4. In a match: `player` for camera readback, `command` for allowlisted console commands.
5. The id counter: start at `max(last id seen in control-result.json for this pid, 0) + 1`. Keep it monotonic within a game process.

### 3.3 Alternative: the DW channel

Use only when the owner runs the DW build. It gives native `map_refresh`, `map_restart`, `fast_restart`, `mw_setpos`, `mw_setang`, `mw_getpos`, `screenshotJpeg`, and `dw_localTestKey`. It needs `.enabled` at launch (manual), a log-based reply, and nonce-matched log lines (the editor should match the dispatch line after its own write offset, as `live.py` does). Reimplement the ~60 lines of `send()` in C++ rather than shelling out to `live.py`, so the editor has no dependency on `E:\mw124`.

### 3.4 What needs a proxy change

| Need | Why the control file cannot do it | Proposed change |
|---|---|---|
| Camera sync at 10 Hz or more, both ways | One request per 250 ms, a file write per request, no push | Socket (section 5). |
| Reliable camera set | `mw_setpos` via `command` is UNVERIFIED; no typed action | Typed `camera_set` op with bounds (same bounds as `Coordinate()` at `noclip.cpp:14-24`). |
| Command output capture | Result has no output field | Add `log` tail or `output` field to the result, or a socket reply. |
| Start match, load a map by id | No action | A typed op that calls the same path as the lobby Start Match. Needs RE of the lobby button. UNKNOWN. |
| Entity hot update | No hook; `map_refresh` only swaps techsets (DW message) | Rebuild, or a script-entity reposition hook. UNKNOWN feasibility. |

---

## 4. Safety: commands that must never be sent automatically

The editor must never send these, not even as a side effect of a build or deploy. Each needs an explicit user action per use, and most should be off by default.

| Item | Why | Evidence |
|---|---|---|
| `hotkey enter`, `hotkey escape`, `hotkey up/down/backspace` outside the browser | Presses lobby or menu controls; can start a match or open the menu | `[PX]developer_ui.cpp:251-287` |
| `hotkey f6` / `f7` / `grave` | Toggles browser and console; `f6` in a non-lobby state can flip browser state | `:251-287`, `:129-140` |
| `key ...` (wasd, space, escape) | Gameplay input with timed holds | `:288-314` |
| `command` with `disconnect`, `quit`, `exit`, `map`, `devmap`, `map_restart`, `fast_restart`, `map_refresh`, `dw_localTestKey` | Ends or restarts the match; reloads zones; injects keys | `[LV]:174-175` (map_refresh, 180 s timeout); `[DW]DW_LocalPlatform.cpp:487` (the channel runs any text through the native parser) |
| `command` with `set` on `LPSPMQSNPQ` (systemlink), `ui_mapname`/`NSQLTTMRMP` to a non-custom id, `MOLPOSLOMO` | Turns off the gates or puts the lobby on a state the owner did not choose | `[PX]developer_ui.cpp:70,176,197` |
| `command` with `exec`, `writeconfig`, `bind`, `unbind`, `seta`/`setu` of `com_*`, `fs_*`, `sv_*` | Writes config, changes the file system or binds | Not in the editor's allowlist; general engine risk |
| Any `command` containing `;` | Multi-command; the DW channel rejects it and it is not audited here | `[LV]:92`, `[DW]DW_LocalPlatform.cpp:474` |
| `command` with `noclip` | Changes the player's movement state | `[PX]noclip.cpp:30-108` |
| `read` at arbitrary RVAs | Reads game memory; bounded but still unreviewed | `[PX]developer_ui.cpp:361-385` |
| `console toggle-output` / `open` during play | Changes UI state | `:336-345` |
| `browser open` (it calls `Refresh`) without a need | Rescan side effect | `:129-140` |
| Writes to `[RPL]` other than `.proxy\control.ini` and `replay-dw-test.command` | The install is the owner's working game | `[DOC19]` §4 |
| Creating `replay-dw-test.enabled` | Changes the DW build's launch behaviour | `[LV]:159-161` (`enable`) |
| Starting or stopping the game (`Run_*.cmd`, `QProcess::startDetached`, kill) | The owner starts the game | `[RPL]Run_*.cmd`; `E:\IW8-Radiant\src\qtapp\main_window.cpp:743-749` (existing user-clicked LaunchReplay, keep it user-only) |
| Mouse input (`click`, `rclick`, `wheel`) | Clicks the real window | `[LV]:188-196` |

Rules to enforce in the editor:
- A command allowlist in code. Free text only in an "advanced" box that is off by default and logs every send.
- Refuse any text with `;` or a control character. Refuse ids that do not match `[a-z0-9_]{4,15}` with the `mp_` prefix (`[PX]custom_maps.cpp:37-44`).
- Only send while a live link is "connected" (`status` answered with the current pid).
- Keep `control.ini` writes to one at a time; wait for the matching result (or 5 s timeout) before the next.
- Never retry a `command` automatically. A timeout means "unknown", and the user decides.

---

## 5. Proposed minimal protocol

Two options. Start with A, then build B only if the owner approves a proxy socket.

### 5A. Over the existing control file (no socket, no protocol change)

Use the actions in section 1.3 as they are. Editor features: status, select, command (allowlisted), player readback. Expected latency: 250 ms to 1 s. Suitable for map selection and occasional commands, not for camera drag.

### 5B. JSON lines over localhost TCP (proxy change, owner approval needed)

- Bind `127.0.0.1` only. Port default `3700` (the iw3xo default, `[IW3]remote_net.cpp:93`), configurable as `live_port` in `mw120rproxy.ini`. Off unless `live_enabled=1`.
- Listen socket non-blocking. Accept one client. Read newline-delimited JSON, max 4 KB per line. Drain the request queue from the main thread in the same place as `PollControl` (`[PX]developer_ui.cpp:448-455`).
- Requests (`v` = 1):
  - `{"v":1,"id":7,"op":"status"}`
  - `{"v":1,"id":8,"op":"select","map":"mp_example"}`
  - `{"v":1,"id":9,"op":"command","text":"set r_fullbright 1"}` (allowlist applied)
  - `{"v":1,"id":10,"op":"camera_get"}`
  - `{"v":1,"id":11,"op":"camera_set","origin":[x,y,z],"angles":[pitch,yaw,roll]}` (bounds as `Coordinate()`, `:14-24`)
  - `{"v":1,"id":12,"op":"subscribe","topic":"camera","hz":10}`
- Replies: `{"v":1,"id":7,"ok":true,"pid":123,"frontend":false,"map":"...","selected":"...","console":false,"browser":false}`
- Push events (only after `subscribe`): `{"v":1,"event":"camera","origin":[...],"angles":[...]}`.
- Errors: `{"v":1,"id":9,"ok":false,"error":"not_allowed"}`. Error codes are a fixed list.
- Versioning: `v` must match. Unknown `op` is an error.
- The socket must never be an arbitrary-call surface. That is the design rule the proxy comment already states (`[PX]developer_ui.cpp:207-208`). The owner must decide whether to relax it.

Proxy files that would change for 5B: a new `live_link.cpp`/`.h` (socket + queue), a hook in `developer_ui.cpp` next to `PollControl` (`:448-455`), a config key in `config.h` (`:9-19`), and the build (`xmake.lua`). UNKNOWN: whether `DrawOverlay` runs during gameplay, which decides whether camera push works in a match (`[PX]developer_ui.cpp:448-455`).

---

## 6. Editor implementation plan

### 6.1 UI

- New dock "Live Link" in the Replay area, next to "Show Replay Log" (`E:\IW8-Radiant\src\qtapp\main_window.cpp:752`, dock registration pattern at `:266`).
- Top row: connection state (`not running` / `running, pid N` / `connected: lobby` / `connected: match`), last result id, and the `map` and `selected` values from `status`.
- Map row: package combo (from the editor's own deploy list), "Select in Replay" (`select`), "Rescan" (see the note in 6.2).
- Console row: allowlisted command combo, plus an "Advanced" toggle (off) for free text.
- Camera row (in match only): readback label from `player`, "Send editor camera" (disabled until P1 is verified), a poll rate setting (default off).
- Log: reuse `ReplayLogPanel` (`E:\IW8-Radiant\src\qtapp\replay_log_panel.cpp`, which already tails `replay-dw-client.log` and `mw120rproxy.log`, `:80-83`). Add a filter for `[Control]` and `[Cbuf]` lines.
- After a deploy, the existing message says to press R in the browser (`E:\IW8-Radiant\src\qtapp\compile_panel.cpp:551-555`). The live link offers a button "Rescan and select this map" that sends `select`, with the user's click as the confirmation.

### 6.2 Core and files

- `E:\IW8-Radiant\src\qtapp\live_link.h` / `live_link.cpp` (new): control-file writer (temp file, `MoveFileEx`), result reader (QTimer ~300 ms, match `pid` and `id`), request queue with one in flight, 5 s timeout, allowlist.
- `E:\IW8-Radiant\src\qtapp\live_link_panel.h` / `.cpp` (new): the dock.
- `main_window.cpp`: create the dock, register an action "Live Link" next to "ShowReplayLog" (`:752`). Do not add auto-launch or auto-select.
- Tests: `E:\IW8-Radiant\src\qtapp\qt_gui_test.cpp` already uses a scratch game folder (`:844`). Test control-file writes and result parsing against a scratch folder only. Never point tests at `E:\IW8\Builds\1.20-replay`.
- Rescan without select: the editor should not use `hotkey f6`/`escape` (section 4). Rescan happens through `select`, or through the existing user step (F6 in the game). Mark standalone rescan as not offered until verified.

### 6.3 Phases

- **P0 (no proxy change):** status, select, command (allowlisted), player readback, log filter. Verify on the owner's machine with a scratch match first.
- **P1 (no proxy change, experimental):** camera set via `command cmd mw_setpos ...`. Verify the `cmd ` route and the noclip side effects on a scratch match first.
- **P2 (proxy change, owner approval):** section 5B socket, `camera` subscribe, command output field.
- **P3:** entity hot update. Needs a separate design; likely script-entity reposition only. UNKNOWN.

### 6.4 Open UNKNOWNs to check on the owner's machine (read-only where possible)

- U1. Whether `DrawOverlay` runs in gameplay (decides camera push and `player` readback in match).
- U2. Whether `command cmd mw_setpos x y z yaw pitch` reaches `noclip.cpp` on the proxy build.
- U3. Whether `command map <id>` or `devmap` loads a custom package (`[DOC19]` §2.7 UNKNOWN).
- U4. Whether `command set <name> <value>` is applied for arbitrary dvars on the proxy build (no readback exists).
- U5. Whether the proxy build that is live is the one `[PX]` describes (`[DOC19]` row 12 and 1.2 say the log and DLL do not match).
- U6. Whether `;` in a `command` value is split by the engine (not tested; the editor refuses it anyway).

---

## Files consulted

- `E:\IW8-Radiant\docs\research\19_replay_test_loop.md`, `05_runtime_evidence_1_20.md`, `01_iw3xo_feature_audit.md`, `E:\IW8-Radiant\docs\FEATURE_MATRIX.md` (section M).
- `E:\mw124\mw120rproxy\mw120rproxy\developer_ui.cpp`, `custom_maps.cpp`, `custom_map_ui.cpp`, `noclip.cpp`.
- `E:\mw124\mw120rproxy\tools\live\live.py`.
- `E:\DW\IW8\integration\replay\DW_LocalPlatform.cpp`, `DW_LocalConfig.cpp`.
- `E:\IW8\Builds\1.20-replay\.proxy\control.ini`, `control-result.json`, `replay-dw-client.log` (tail), `Run_*.cmd` (read only).
- `E:\IW8-Radiant\reference\iw3xo-radiant\src\components\modules\remote_net.cpp`, `src\game\structs.hpp`.
- `E:\IW8-Radiant\src\qtapp\main_window.cpp`, `compile_panel.cpp`, `replay_log_panel.cpp`.
