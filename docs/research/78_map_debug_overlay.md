# 78. In-game map debug overlay (`iw8r_mapDebug`)

Status: client source written and compile-checked; **nothing has been run in game**. The full DW validation build
could not run (see section 8). Date: 2026-11-11.

Marks used below: **VERIFIED IN CODE** = read in the older exe's code (IDA, `game_dx12_ship_replay.exe`) or in a
source file. **INFERRED** = follows from code or data layout but was not read in the function that matters.
**NEEDS A LIVE TEST** = can only be settled by running it. Every on-screen behaviour is NEEDS A LIVE TEST, because
nothing was run.

Files:
- New: `E:\DW\IW8\integration\replay\DW_MapDebug.cpp` (one file, about 1,000 lines).
- Registration (two edits, three lines): `tools/dw_client.py` line 587 adds `"DW_MapDebug.cpp",` to `replay_additions`
  after `"DW_Devmap.cpp"`; `integration/replay/Con_Developer.cpp` declares `void DW_MapDebug_Frame();` (line 13) and
  calls it in `Con_DeveloperFrame` right after `DW_Devmap_Frame();` (line 230), before the console is drawn.

---

## 1. What the overlay shows

It is meant for a mapper standing in their own map in a private match they host. Most of it reads the **server**
entity list, which only exists in the process that runs the server. In a match hosted by someone else the panels
still work, but there are no entity labels or boxes.

| Bit | What | Source |
|---|---|---|
| 1 | Label at every entity near the player, nearest first: `#num classname` (or the ET_ type when there is no classname), `tn <targetname>  sn <script_noteworthy>`, `model <xmodel name>`, `(x y z) <distance>u ET_type`. A small XYZ axis marker (red +X, green +Y, blue +Z, 12 units) at the origin. Trigger labels are orange, others yellow. | server `g_entities` |
| 2 | Wire box for each `trigger_*` entity (orange), from the bounds the engine keeps (`r.box` midpoint and half size, rotated by the entity's angles). | server `g_entities` |
| 4 | Top-left panel: map (`ui_mapname`) and game type (`g_gametype`); eye position and view pitch/yaw; feet position of your player entity; server entities in use, used slots and the 2046 limit; active audio (reverb) zone; how many labels/boxes are shown and the filter. | client view, server, sound |
| 8 | Crosshair inspector (panel right of and below the screen centre): one physics trace per frame along the view: hit point, distance, normal, **surface type number and name**, raw surface flags, stairs bit, hit type, hit entity (with classname/targetname/model when hosting), and the raw result words after the normal. A white cross on the hit point and a short cyan line along the normal. | client physics world |
| 16 | Wire box for every other entity that has bounds (cyan): script_brushmodels, script_models with collision, and so on. | server `g_entities` |
| 32 | Also include players, AI, corpses, missiles, items and client characters in bits 1, 2 and 16 (hidden by default to keep the screen readable). | |

Suggested values: `iw8r_mapDebug 7` (labels, trigger boxes, panel), `15` (with crosshair inspector), `63` (all).

## 2. Dvars and command

| Name | Type, range, default | Meaning |
|---|---|---|
| `iw8r_mapDebug` | int 0..255, default 0 | Bit mask above. 0 = off; when off the per-frame cost is one dvar read. Only bits 1..32 are used. |
| `iw8r_mapDebugDist` | int 64..65536, default 2000 | Entities further than this (map units, from the eye) get no label or box. |
| `iw8r_mapDebugMax` | int 1..256, default 48 | Labels and boxes per frame; the nearest ones win. |
| `iw8r_mapDebugFilter` | string, default empty | Space separated terms. An entity is shown when any term is a case-insensitive substring of its classname, targetname, script_noteworthy, target or model name. A term starting with `-` hides matches. Examples: `trigger`, `spawn -tdm`, `minimap_corner`. |
| `iw8r_mapDebugDump` | command | Writes once to `replay-dw-client.log` (beside the exe): `IW8R_MAPDEBUG begin ...`, `IW8R_VIEW x y z pitch yaw`, `IW8R_FEET x y z entnum`, `IW8R_AUDIO ...`, a column header `IW8R_ENT_COLUMNS ...`, one `IW8R_ENT` line per server entity in use, then `IW8R_MAPDEBUG end entities=N`. Fields are `|` separated (the dump replaces `|` and `^` inside strings with `?`). Prints a one-line result to the console. |

`IW8R_ENT` columns: `num|type|classname|code_classname|targetname|target|script_noteworthy|script_linkname|script_linkto|model|origin|angles|box_mid|box_half|model_type|linked|parent`.
Vectors are three space-separated numbers with two decimals. `parent` is -1 when not linked.

The dvars are registered (flags 0, not saved) and the command is added on the first UI frame, in every DW client
(also in the front end). Type them in the DW developer console (grave or F7). The console's dvar-name translation only
touches names it knows, so these new names pass through unchanged (VERIFIED IN CODE, `Con_Developer.cpp`
`Translate`).

When the overlay draws (all VERIFIED IN CODE in `DW_MapDebug_Frame`): DW backend enabled, main thread, `iw8r_mapDebug`
non-zero, the front end scene not active, local client 0 active, the small dev font and `scrPlaceFull` sane, and the
view in `LocalClientGlobals[0]` decodes to finite values with a unit forward axis. Entity data additionally needs
`SV_Loaded()` and a readable entity array.

## 3. Engine functions and globals used

Older build = `game_dx12_ship_replay.exe` (IDA, image base 0x140000000). Recompiled = name and address in
`game_dx12_ship_replay_dw.map`. Every undefined symbol of the compiled object was checked: each name occurs exactly
once in that map (the compiler's `_fltused` is the only exception; `Com_DebugStates.cpp` has the same reference). No
`replay_rva_` names are used.

| Name (catalog) | Older build | Recompiled | Use | Status |
|---|---|---|---|---|
| `g_entities` | 0x14BC20F00 | 0x14BC8CF00 | pointer to the gentity array, stride 0x5A0 | VERIFIED IN CODE (`G_Debug_EntInfoLine`: `imul 5A0h; add rdi, g_entities`) |
| `s_entityFlagByteTable` | 0x14BC20F08 | 0x14BC8CF08 | pointer to one in-use byte per entity | VERIFIED IN CODE (`G_Debug_PrintEntityDebugInfoToConsole` loop) |
| `g_entityIdxCount` | 0x14BC216D8 | 0x14BC8D6D8 | used entity slots | VERIFIED IN CODE (same loop) |
| `g_gEntityTypeNames` | 0x1441CFF20 | 0x14423BF20 | 29 names ET_GENERAL .. ET_INFO_VOLUME_GRAPPLE | VERIFIED IN CODE (`G_GetEntityTypeName`, table read in IDA) |
| `s_utilsCachedEntries` | 0x14BC9B498 | 0x14BD07498 | model index (gentity +0x414) to XModel*, name at +0 | VERIFIED IN CODE (`G_Utils_GetCachedEntryPtr` + `G_Debug_EntInfoLine` asset column) |
| `SL_ConvertToString` | 0x14131AA20 | 0x141351A20 | script string index to text (pure arithmetic on the string pool) | VERIFIED IN CODE |
| `LocalClientGlobals` | 0x14F26F940 | 0x14F2DB940 | `cg_t*` per local client | VERIFIED IN CODE |
| `cls_whiteMaterial` | cls+0x150 | 0x14EF1CC70 | material used by `DevGui_DrawLine`; checked non-null before drawing | VERIFIED IN CODE (DevGui_DrawLine reads `cls+336`; map places `cls_whiteMaterial` at cls+0x150) |
| `cls_smallDevFont`, `scrPlaceFull` | | 0x14EF1CC60, 0x14EF99EA0 | text font and screen placement | as `Com_DebugStates.cpp` |
| `CL_DrawText` (= older `CG_HudElem_DrawLegacyFontText` 0x1415AAAA0) | 0x1415AAAA0 | 0x1415F0AA0 | text | VERIFIED IN CODE; same call as `Com_DebugStates.cpp` |
| `R_TextWidth` | 0x141932DA0 | 0x14198ADA0 | panel width | as `Com_DebugStates.cpp` |
| `DevGui_DrawLine` | 0x1417EC6D0 | 0x1418436D0 | rotated quad line in pixel space: (start[2], end[2], thickness, RGBA bytes) | VERIFIED IN CODE (body); that it shows in a match: NEEDS A LIVE TEST |
| `UI_FillRect` | 0x141B9B700 | 0x141BFD700 | dark panel background; draws nothing while `s_uiFillRectMaterial` is null | VERIFIED IN CODE |
| `PhysicsQuery_LegacyTrace__Atlas_EFFA20` | 0x14109A630 | 0x1410C0630 | crosshair trace, arguments as `CG_ViewMP_ComputeWeaponAimTrace` (world `lcn + 2*(lcn+1)` = 2, `bounds` zero, pass entity, 1, contents, 0, null, 0) | argument order VERIFIED IN CODE (disassembly of the aim trace call); world 2 and mask 0x2806031 INFERRED |
| `Com_SurfaceTypeToName` | 0x1413F4FD0 | 0x141436FD0 | surface type name | VERIFIED IN CODE |
| `CG_GetCurrentAudioZone` | 0x1416F3930 | 0x141748930 | primary/secondary zone record and blend | VERIFIED IN CODE (body); zone record names at +64/+96 INFERRED from the converter (`iw3_sound.cpp` zones table, stride 200) |
| `Dvar_RegisterInt`, `Dvar_RegisterString`, `Dvar_FindVarByName`, `Cmd_AddCommandInternal`, `Com_sprintf`, `Com_Printf`, `DW_LocalBackend_LogLine`, `SV_Loaded`, `CL_IsLocalClientActive`, `Com_FrontEndScene_IsActive`, `Sys_IsMainThread` | | in map | registration, logging, gates | as other DW files |

Field offsets (all VERIFIED IN CODE unless marked):

- gentity (stride 0x5A0): number int16 +0x0; eType uint16 +0x8; `r.isLinked` +0xF8; `r.modelType` +0xF9;
  `r.box` midpoint +0x100, half size +0x10C (local); `r.absBox` +0x118/+0x124 (world, +1 padding, not used);
  `r.currentOrigin` +0x130; `r.currentAngles` +0x13C; classname +0x17C, code classname +0x180, script_linkname
  +0x184, script_linkto +0x188, script_noteworthy +0x18C, target +0x190, targetname +0x194 (all script string
  indices); tagInfo pointer +0x2C0 (its first qword is the parent gentity); model index uint16 +0x414.
  Sources: `G_Debug_EntInfoLine` 0x141212090, `SV_LinkEntity` 0x141391C10, `SP_trigger_radius` 0x1412777A0,
  `G_Trigger_MathTriggerAABBQuery` 0x141276DA0. Note: the IDA type `gentity_s` says size 0x5B0, but the code uses
  0x5A0; the code wins.
- Entity limit 2046: ENTITYNUM_WORLD is 0x7FE (`Trace_GetHitEntityNum`, `G_Bullet_TraceAndSpawnImpactFX`). VERIFIED.
- cg_t view (from `CG_LocalClient_ProjectWorldToScreen` 0x141696AA0 and `CG_LocalClient_ProjectPointByView`
  0x1416964F0): tan(half fov) X +0x6810, Y +0x6814; view origin at +0x6818 is XOR-obfuscated: key = dword +0x6874,
  for each axis `a = (uint32)(cg + 0x6818 + 4i) ^ key; value = dword ^ (a * (a + 2))`; view axis at +0x6824
  (forward, left, up). Screen position `x = (1 - dot(d,left)/tanX/depth) * width/2`,
  `y = (1 - dot(d,up)/tanY/depth) * height/2`, width/height = `scrPlaceFull` realViewportSize (+32/+36). The overlay
  evaluates this formula itself, so it does not call the projector per point.
- Trace result (from `Scr_AIPhysicsTrace` 0x141248000 and `Trace_GetHitEntityNum` 0x141295A60): fraction +0x0,
  normal +0x10, surface flags +0x1C (surface type = bits 19..24, stairs = 0x200), hit type +0x24 (0 none, 1 entity,
  2..6 world or other), hit id +0x2C (entity number for type 1), a "solid" byte at +0x3C.
- Screen alignment 5 (both axes) is "no transform" in `ScrPlace_ApplyRect` 0x1415DABC0 and `ScrPlace_ApplyX/Y`, so
  labels are drawn in real pixels with a font scale of `0.75 * scaleVirtualToReal.y` (the panels use the virtual
  alignments of `Com_DebugStates.cpp`).

## 4. Status of each requested item

| # | Item | Done? | Status |
|---|---|---|---|
| 1 | Entity labels: classname, targetname, script_noteworthy, entity number, origin, model, distance | yes | reading VERIFIED IN CODE; drawing NEEDS A LIVE TEST |
| 1 | Classes covered | all server entities in use (script_model, script_brushmodel, script_origin, trigger_*, spawn points if the game keeps them as entities, dynamic props that are server entities). Spawn points: INFERRED that MP spawn points stay server entities after load (`mp_*_spawn` are usually removed into a script array by `maps\mp\gametypes` code; if so they will not show). Client-only things (dynents, static models, scriptables that are not entities, client triggers) are not in this list. | NEEDS A LIVE TEST |
| 1 | Distance limit, filter | yes | VERIFIED IN CODE |
| 2 | Crosshair: hit position, normal, distance, entity hit | yes | layout VERIFIED IN CODE; calling the trace from this point in the frame NEEDS A LIVE TEST |
| 2 | Surface type / flags | yes: type number and name, raw flags. This is exactly the value the converter writes (`material << 19` in the shape userData, research 71). | NEEDS A LIVE TEST |
| 2 | Material name of the hit surface | **no**. The trace result has no material; Havok collision does not carry the render material. | — |
| 2 | Physics material (the PM_Concrete CRC, research 71) | **not identified**. The two raw-word rows show every result word after the normal so a live test can look for 0x1AB7BC33. | NEEDS A LIVE TEST |
| 3 | Origin and angles in map units | yes: eye origin (decoded view origin), pitch and yaw from the view axis (roll not shown), and the feet origin of your player entity (nearest ET_PLAYER within 160 units of the eye; host only) | decode VERIFIED IN CODE |
| 3 | Map name | `ui_mapname` and `g_gametype` dvar text (the lobby's values, which is what a private match starts) | INFERRED that `ui_mapname` equals the loaded map |
| 3 | Visibility cell | **no**. No cheap, verified reader was found. | — |
| 3 | Active reverb / audio zone (research 72) | yes: zone name, reverb name, zone id, and the blend target/lerp when two zones mix | function VERIFIED IN CODE; record field offsets INFERRED |
| 3 | Reflection probe (research 73) | **no**. Not reachable without unverified renderer state. Candidate built-in dvars are listed in section 5. | — |
| 3 | Entity counts and limits | server: in use, used slots, limit 2046. Client entity counts: **no**. | VERIFIED IN CODE |
| 4 | Trigger volumes and other invisible things as wire boxes | yes, from `r.box` (local) rotated by `r.currentAngles` at `r.currentOrigin` | layout VERIFIED IN CODE. For `trigger_radius` (a cylinder) the box is the cylinder's bounding box (`SP_trigger_radius` stores radius, radius, height/2). What `modelType` 2 and 3 mean is INFERRED (the AABB query treats them as sphere-like/cylinder). Client triggers (audio zones, `clienttriggers`) are a different list (`cm_mapEnts`) and are **not** drawn. |
| — | Log dump for the editor | yes, `iw8r_mapDebugDump` | NEEDS A LIVE TEST |

Thread safety (INFERRED): the server runs on its own thread (`Sys_IsServerThread` exists). The overlay only reads
server memory, from the main thread, so a value can be one frame stale or torn while an entity spawns or frees. Every
entity is re-checked (its number field must equal its slot, its type must be below 29, every float must be finite),
the entity array and flag table are range-checked once per frame, and every string is checked page by page before it
is copied. The crosshair trace runs against the **client** physics world on the main thread, as cgame does; it is
off unless bit 8 is set, because a trace from this point of the frame has not been proven safe.

## 5. Built-in debug facilities found

| Facility | Older build | State | Notes |
|---|---|---|---|
| `dumpEntInfo` client command → `G_Debug_PrintEntityDebugInfoToConsole(0, clientNum)` → `G_Debug_EntInfoLine` | 0x141212680, 0x141212090; string at 0x142462CA0 | **alive**: full bodies, called from `G_CmdsMP_StringCommandDispatch` 0x14120B6A0 and `G_Cmds_ClientCommand` 0x141827C60 on the string `dumpEntInfo` | Prints a CSV (Number, Type, Distance, Classname, CodeClassname, Asset, Targetname, Target, ScriptNoteworthy, ScriptLinkname, ScriptLinkTo, Team, ParentNum, Origin, Angles, SentToClient, SpawnCallstack) with `Com_Printf` channel 15. Try `cmd dumpEntInfo` in the console. Whether a cheat or developer gate sits before it, and whether channel 15 shows in the DW console, NEEDS A LIVE TEST. The overlay reads the same fields itself instead of calling it, because it also calls script-context and snapshot functions that belong to the server thread. |
| `dumpEntInfoCSV` → `G_Debug_PrintEntityDebugInfoToCSV` | 0x1412124E0 | alive (same dispatchers) | Writes a CSV; where it writes was not checked. |
| `CG_LocalClient_ProjectWorldToScreen` / `ProjectPointByView` | 0x141696AA0 / 0x1416964F0 | alive (LUI waypoints, nameplates) | Formula reused. |
| `DevGui_DrawLine` | 0x1417EC6D0 | alive body, caller `DevGui_DrawGraph` | Used for the wire boxes. |
| `CG_HudDraw_DrawLineQuad` | 0x1416958A0 | alive (`CG_VehicleCam_UpdateSkeletonPose`) | Alternative line drawer; needs a material argument; not used. |
| `CG_GetCurrentAudioZone`, `CG_FindAudioZoneAtPoint` | 0x1416F3930, 0x1416F3110 | alive | First one used. |
| `CgHandler::DebugLineAll` | 0x140C91FC0 | not a drawer: an animation-state dispatch stub | Dead end. |
| `GScr_PrintAllDebugStrings` | 0x141230B60 | script print to the console, no callers in code (script builtin) | Not a drawer. |
| `R_AddDebugCross`, `CL_GetDebugStrings`, `Stream_Debug_DrawDebugOverlays` | names only in the recompiled map (0x140D7DE40, 0x141723780, 0x14102AFE0) | **not checked**: these names are not in the older IDB, so their bodies and flush path were not verified | Not used. A debug queue whose flush is stripped would draw nothing. |
| Developer dvars (console translates the names): `cg_drawViewpos`, `g_entinfo_maxdist`, `g_entinfo_type`, `bg_entinfo`, `r_reflectionProbeShowVolumes`, `r_reflectionProbeVolumeShowInfo`, `r_reflectionProbeShowDefaultProbe`, `r_showPortals`, `r_singleCell`, `snd_drawInfo`, `r_showTriCounts` | tokens in `con_dvar_names.h` | **unknown**: registered names exist; whether any consumer still draws in this ship build was not checked | Worth a 1-minute live try each (section 6, test 7). |

## 6. Live tests for the owner, shortest first

Setup for all: DW client with this file built in, a private match on your map (you host), DW console open with
grave or F7. Nothing needs a restart between tests. On any failure send back: the test number, what you saw (a
screenshot helps), and the last 50 lines of `replay-dw-client.log` beside the exe; if the game crashed, the crash
dump folder name and time.

1. **Dvar exists.** Type `iw8r_mapDebug`. Pass: the console shows the dvar with value 0 (or no "unknown command"
   error). Fail: unknown command → registration did not run; send the log.
2. **Panel.** `iw8r_mapDebug 4`. Pass: a dark top-left panel with map name, `eye x y z pitch yaw`, `feet`, entity
   counts, audio zone. Walk a few steps: eye/feet numbers change; the feet z should be your floor height in the
   editor. Compare with `cmd mw_getpos` (DW_POS in the log). Fail: nothing drawn, or numbers that do not move →
   send a screenshot.
3. **Labels.** `iw8r_mapDebug 1`. Pass: yellow/orange labels at entities near you, each with a short red/green/blue
   axis marker that sits exactly on the object. Turn around: labels stay on their objects. Fail: labels offset by a
   constant (send a screenshot facing a known script_model), mirrored, or missing (send `iw8r_mapDebugDump` output).
4. **Trigger boxes.** `iw8r_mapDebug 3`, stand near a trigger you placed. Pass: orange wire box matches the
   editor's trigger brush. Fail: no lines at all (the line drawer did not draw; labels still there?) or a box of the
   wrong size/rotation (send the box_mid/box_half from the dump line and the editor values).
5. **Dump.** `iw8r_mapDebugDump`. Pass: console says "N entities written"; the log has `IW8R_MAPDEBUG begin`,
   `IW8R_VIEW`, `IW8R_ENT_COLUMNS`, N `IW8R_ENT` lines, `IW8R_MAPDEBUG end`. Send the whole block back either way;
   it is also the answer to "which of my entities survived the load" (spawn points, minimap corners, research 75).
6. **Crosshair inspector (research 71).** `iw8r_mapDebug 12`. Look at a wall, a floor, a crate whose surface type you
   set in the editor. Pass: `surface <n> <name>` changes with what you look at and matches the editor's
   `surface_type`; a white cross sits on the surface. Send back, for three surfaces: the `surface` row and both
   `raw` rows (look for 1AB7BC33, the PM_Concrete CRC). Fail: the game freezes or crashes the moment bit 8 is set
   → send the crash info; do not use bit 8 again until the trace call is changed.
7. **Audio zone (research 72).** With bit 4 on, walk into the hall zone. Pass: the `audio zone` row changes from the
   map zone to `hall` (reverb `hall_reverb`). If it never changes, note your feet z against the zone box z range
   (104 to 264 in research 72). Then try the built-in dvars from section 5 one at a time, e.g. `snd_drawInfo 1`,
   `r_reflectionProbeShowVolumes 1`, `cg_drawViewpos 1`, and say which draw anything.
8. **Built-in dump.** `cmd dumpEntInfo`. Pass: a CSV entity list in the console. Report whether it appears.

## 7. What the editor could add later

- **Live Link button** "Debug overlay": sends `iw8r_mapDebug 7` (or 15) and `iw8r_mapDebugFilter <selected entity's
  targetname>` through the existing Live Link command path, so selecting an entity in the editor highlights it in game.
- **Read the dump**: after a "Dump entities" button sends `iw8r_mapDebugDump`, parse the last `IW8R_MAPDEBUG begin`
  .. `end` block from `replay-dw-client.log`, match `IW8R_ENT` lines to editor entities by targetname/classname and
  origin, and list: entities that did not survive the load, origin drift, triggers whose bounds differ from the brush.
- **Jump to player**: `IW8R_VIEW` / `IW8R_FEET` move the editor camera to where the player stands (same map units).
- **Surface check**: a panel that compares the inspector's surface type for a picked face with the editor's
  `surface_type` (needs test 6 first).

## 8. Validation

- Compile check with the DW flags (`clang++ --target=x86_64-pc-windows-msvc -ffreestanding -ffunction-sections -O2
  -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti`): `DW_MapDebug.cpp` and the edited
  `Con_Developer.cpp` both compile with no warnings. The object defines only `DW_MapDebug_Frame`.
- Undefined symbols: 34; 33 are each found exactly once in `game_dx12_ship_replay_dw.map`; `_fltused` is the compiler's
  float marker (also referenced by `Com_DebugStates.cpp`). Square roots use SSE (`_mm_sqrt_ss`), sin/cos/atan are
  local, so no CRT maths is pulled in.
- `python -B -c "import ast; ast.parse(open('tools/dw_client.py').read())"` passes after the edit.
- Full validation build (`tools/build-dw-client.py --validate-only --carry-installed-gulag --compiler-jobs 2`) was run
  once and stopped in prepare, before any compile: `verify-installed-gulag-carryforward.py` exited 1 (the main session
  reports its message as "Installed client or original Replay image differs"). This is the DW project's own state
  and not this file; nothing in the pipeline was changed.
