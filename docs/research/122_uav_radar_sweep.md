# 122. The UAV's minimap sweep and enemy pings

**Why.** Owner playtest 2026-11-11: on a built map of about 544 x 544 units (two `minimap_corner` script_origins at
(-272,-272,0) and (272,272,0), worldspawn `northyaw 90`) a UAV gave no radar sweep and no enemy pings on the minimap.
The minimap image itself was right, bots were the enemies, other killstreaks worked, and the UAV works on stock maps.
This note lists every input the client's sweep and pings read in the Replay exe, and which of them a built map can
change.

**Labels.** VERIFIED = read in the Replay exe (`game_dx12_ship_replay.exe`, Hex-Rays / disassembly, address given) or
in a file (path:line). INFERRED = follows from verified facts, not read or run. Nothing was run in the game. Function
names starting `CG_Compass_` were given by an automatic pass; the notes below say what the code does.

## 1. What the game does

### 1.1 The minimap rectangle (`setminimap` -> config string 10)

| Step | Function, address | What it reads / writes | Label |
|---|---|---|---|
| Server builtin | `GScr_SetMiniMap` `0x1412517B0` | Args: material, ULx, ULy, LRx, LRy, numTiles (>= 1), optional 2nd material (feature 79). Rotates (LR - UL) by north: with feature 187 from `g_mapNorthYawCosSin`, else from config string 9 (northyaw, degrees). Script error 4518 if the rotated width or height is < 0. Writes config string 10 = `"mat" ["mat2"] ULx ULy LRx LRy numTiles`. | VERIFIED |
| Client parser | `CG_DrawTools_MiniMapChanged` `0x141696190` (called from `CG_MainMP_Init`, map init and config-string change) | cg+102160 = northyaw (cs 9), +102164 cos, +102168 sin, +102176 map material, +102184 `<mat>_tdmd` material (only if `TopDownMapData_HasData`), +102192 ULx, +102196 ULy, +102200 width = dx*sin - dy*cos, +102204 height = -(dy*sin + dx*cos), +102212 numTiles. A width or height of exactly 0 is replaced by 1000.0 (stored as 1148846080 = 0x447A0000). No check on map name. | VERIFIED |

In the sweep code these fields appear as `cg->_pad188D4[1600..1636]` (0x188D4 + 1600 = 102164: cos, +1604 sin,
+1628 ULx, +1632 ULy, +1636 width).

### 1.2 Sweep and pings (client, every frame)

| Step | Function, address | What it reads | Label |
|---|---|---|---|
| Gate (pings) | `CG_Compass_UpdateSightingPulsesIfVisible` `0x141777770`, from `CG_HudMP_UpdateLocalClientFrame` | cg+0x8271B must be 0; compass+0xFA58 (`drawRadarSweep`) != 0; then ps+756 (cg+0x2FC) != 0, **or** ps+760 (one-off sweep start time) > 0 and still inside one period. Then compass+0xFA59 must be 0. Sweep phase cg+0x824D4 = frac(time / period + previous). Otherwise phase = 0, nothing happens. | VERIFIED |
| Period | `CG_Compass_GetSightingHoldTimeMs` `0x141769A90` | ps+756 == 0 -> 4000 ms; ps+759 (radar mode) 3 -> 8000, other nonzero -> 2000, 0 -> 4000. | VERIFIED |
| Sweep line in world | `CG_Compass_ComputeEdgeMarkerPosition` `0x141777450` | Line normal (sin, -cos); thickness T = max(`cg_compassMapRadarSweepThickness` * width, R * sqrt2 + R * `cg_compassRadarSweepThickness`), R = vfunc +72 of the compass object; offset = n.UL + (width + T) * phase - T/2. `k_CG_compassCornerDistScale` = 0x3FB504F3 = 1.41421. | VERIFIED |
| R | vtable `CgCompassSystemMP_vftable` 0x14251EA68 +72 -> `CG_Compass_GetDetectionRange` `0x141777550` | Non-BR (compass+0xFA59 == 0): dvar `compassDetectionRange` (or the expanded-minimap dvar with perk 57). Default of `compassDetectionRange` = 1500 (`CG_MainMP_RegisterReplicatedDvars`, register at 0x141796F02, `k_float1500`). | VERIFIED |
| Pings | `CG_Compass_UpdateSightingPulses` `0x141778570` | For every client slot (compass+1480 + 136*i): character info valid, entry flags & 3 == 1 (bit 1 = enemy team, bit 2 clear), entry position between the previous and the current sweep line -> `CG_Compass_TryEmitSighting(entry, time, 0x44)`. Also 8 actor-sighting slots, skipped when `bg_compassShowEnemies` is on. No test against the map rectangle. | VERIFIED |
| Entries | `CG_Compass_UpdateCompassEntries` `0x141779010` | Per client entity: in the current snapshot (cent+640 bit 0); bit 2 set first, cleared (and position refreshed) only for a player-type entity without eFlag 0x20000 for which `CG_Entity_IsVisibleToLocalPlayerMP` `0x1416A9000` returns false (that function tests eFlag 0x1000, not line of sight). | VERIFIED |
| Sweep drawn | `CG_Compass_DrawSightingMarkers` `0x141773880`, from `CG_Compass_DrawMapAndMarkerLayers` `0x14176F870` when compass+0xFA59 == 0 | Same gate as the pings (cg+0x8271B, +0xFA58, ps+756/760). Draws `s_compassSightingMarkerMaterial` across the map rect; on the minimap the band position is (2h+1)*phase - h with h = (T/2)/width. | VERIFIED |
| BR/circle mode | `CG_Compass_DrawMapAndMarkerLayers` | compass+0xFA59 != 0: no line sweep; circle pulses from mapinfo columns 41-45 (`TriggerPortableRadarPing`). | VERIFIED |
| `drawRadarSweep` | `LUI_LuaCall_LUIElement_SetupMinimap_impl` `0x141AF2570` -> `LUIElement_Minimap_DrawCompassLayer` `0x141AF1550` -> `CG_Compass_SetSightingLayerEnabled` `0x141778E90` (writes +0xFA58) | Lua table field; true unless the Lua passes false. | VERIFIED |
| Per-map table | `CG_BR_LoadMapInfoCsv` `0x141777860` (from `CG_MainMP_Init`, `CgCompassSystemBR::InitCompass`) | `mp/mapInfo.csv` row by `cls.m_activeGameMapName`: col 17 -> +0xFA59 (BR circle mode), cols 19/20/25 BR ranges, cols 36-40 -> +64136..64152 (marker size factors), cols 41-45 -> circle timings, col 4 (0 -> 1.0), cols 7-9, 34-35. A missing row gives "" -> 0 for all. | VERIFIED |
| Marker size | `CG_Compass_GetMarkerRangeLimit` `0x141775FB0` | When `compassDynamicSelectorSizeEnabled` (default **1**, registered at 0x141794CF4) the marker size is multiplied by the mapinfo factor (cols 37-40). | VERIFIED (default); which markers use it: INFERRED (callers are virtual, not traced) |

ps offsets: `ScrCmd_TriggerOneOffRadarSweep` `0x141261450` writes `level_time` to gclient+760, the same offset the
client reads as ps+760 (VERIFIED). ps+756 is the field the client treats as "has radar" and ps+759 as the radar mode
(VERIFIED in the client; that the server copies `player.hasradar` there: INFERRED, the client-field table was not read).
`ClientScr_SetRadarMode` `0x14100DA20` stores the mode in `sess.cs` (VERIFIED).

### 1.3 Server: which enemies a client is told about

- `SV_BuildServerSnapshot_AddEntitiesClientsCanSee` `0x141383790` adds every entity for which
  `SV_SnapshotMP_CanClientsSeeEntity_0` `0x1413883E0` is true: linked (+248), not no-client (+250 bit 0), and not hidden
  from every client (the 7-dword mask at +1116 not all ones). No map data in this step (VERIFIED).
- The per-client step (`SV_SnapshotMP_EncodeClientEntities`, `TransientVisibility_GetClientSlot`, the
  `g_umbraPortalOpenBits` block copied into each server snapshot) was not read. It may use map visibility data
  (not determined).
- The client pings only enemies that are in its snapshot (1.2, Entries). There is no separate radar list in the client
  path read here (VERIFIED for the client; INFERRED that none exists on the server).

## 2. What a built map passes versus a stock map

Stock: `scripts\mp\compass::setupminimap` (`E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\mp\compass.gsc:4-113`).
Built: the converter's generated `iw3compat_minimap()` (`E:\mw124\x64-zt\src\convert\iw3_iw8\convert.cpp:2255-2302`),
called first in `main()` (`iw3_scripts.cpp:464`, `:469`). Corners from `iw3CompassBounds`
(`games/iw8/dump/maps.cpp:366-473`, ordered in Replay's northyaw-90 frame).

Worked for the 544 map (corners A = (-272,-272), B = (272,272), northyaw 90 -> north (0,1), west (-1,0)):

| | Stock `setupminimap` | Built `iw3compat_minimap` |
|---|---|---|
| UL | B-A = (544,544); dot west = -544 <= 0; dot north = 544 > 0 -> var_11 = A + (0,544) = (-272, 272) (`compass.gsc:68-72`) | northwest = (-272, 272) (k = 1 when `mw120r_minimap_scale` is unset) |
| LR | var_12 = B - (0,544) = (272, -272) | southeast = (272, -272) |
| Aspect step | ratio 544/544 = 1, no change if the aspect dvar is 1 (`compass.gsc:80-99`) | none |
| Call | `setminimap(mat, -272, 272, 272, -272, 1, mat2)` (`:113`) | `setminimap("compass_map_<id>", -272, 272, 272, -272, 1, "compass_map_<id>")` (research 75 section 1.4 read it from a built script) |
| level.mapsize / mapcorners | 544 / corners with yaw | 544 / corners with yaw (`convert.cpp:2282-2299`) |

VERIFIED: the arguments are the same, in the same order and sign. Server check (`GScr_SetMiniMap`): rotated width
= 544*sin90 - (-544)*cos90 = 544, height = -(544*cos90) - (-544*sin90) = 544, both >= 0, no script error. Client:
width 544, height 544. The corner order is **not** the cause.

The worldspawn `northyaw 90` is written by the build (`src\core\compile_iw8.cpp:2522-2528`) and was in the playtest
build (`out\agent\mp_iw8r_fix1\...\mp_iw8r_fix1.d3dbsp.ents:4`) (VERIFIED). The image drawn with the right
orientation shows config strings 9 and 10 arrived (INFERRED).

## 3. Cause: ranked candidates

No map-dependent condition in the client path suppresses **both** the sweep and the pings for this map. Each input
in section 1.2 is either map-independent or has a good value here. So:

1. **The local player's ps+756 (has radar) never became 1, and no one-off sweep ran (ps+760).** The sweep and the
   pings share exactly this gate (`0x141777798`, `0x141773880`, `0x14176F870`), and it is the only gate that a UAV
   changes. When it is false the client draws no line and emits no ping on any map (VERIFIED). Nothing in the engine
   path read here makes it depend on map data, so the reason is on the server/script side between the UAV call and
   `uav.gsc` `_id_13FDF` (`hasradar = 1`, lines 939-950 / 992). That path was not re-read (out of scope by the
   brief) (INFERRED, most likely).
2. **Map info row missing** (only if the map was not answered as a DW package). Without a `mp/mapInfo.csv` row,
   cols 36-40 give 0, and with `compassDynamicSelectorSizeEnabled` = 1 the size factor of the markers is 0
   (`0x141775FB0`). That would hide the ping markers but not the sweep line. The DW client answers every column a
   package does not set with Shipment's row (`E:\DW\IW8\integration\replay\DW_MapAvailability.cpp:788-811`,
   `default:` at `:809-810`; Shipment has 1,1,1,1,1 in cols 36-40 and nothing in col 17,
   `...rawfile_source\mp\mapinfo.csv`), so for an installed package this is not the cause (VERIFIED for the source;
   that the playtest exe was built from it: INFERRED).
3. **A weak sweep, not a missing one.** On a 544-unit map the band thickness T is at least 1500 * 1.41421 = 2121
   units, about 3.9 map widths (`0x141777450`, defaults VERIFIED except `cg_compassRadarSweepThickness`). The line
   crosses the map in about 544 / (544 + 2121) = 20 % of each 4 s period, about 0.8 s, and the gradient band is
   wide. Pings are not affected (each enemy is still crossed once per period). This can make the sweep easy to miss,
   but it does not explain "no pings" (INFERRED).
4. **Enemies not in the snapshot or flagged.** A ping needs the enemy in the client's snapshot and without eFlag
   0x1000 (1.2). The first server step is map-independent (1.3). The per-client step was not read. Bots that were
   seen on the ground were in the snapshot then, so this cannot remove all pings (INFERRED).

Ruled out (VERIFIED unless marked): corner order or sign (section 2); zero or negative world size (544 x 544, and 0
would become 1000, not suppress); a map-name check in the parser (none); minimap rotation (none in this path);
enemies outside the map rectangle (pings do not test it); a missing compass material (the map drew; the sweep material
is the code material `s_compassSightingMarkerMaterial`, not a map asset: INFERRED); BR circle mode (col 17 empty for
Shipment's row; `GScr_SetMiniMap` and the sweep do not read the game mode except features 79 / 187).

## 4. What the build should change

- Nothing in the generated minimap script: the `setminimap` line and `level.mapsize` / `level.mapcorners` already
  match the stock values exactly (section 2).
- To prove candidate 1 offline, the next step is a script-side check (not done here): follow `uav.gsc` from
  `tryuseuav` (line 155) through `launchuav` (308-465) and `addactiveuav` (1136) to `_id_13FDF` (901-951) and list each
  map-dependent call (`getuavrig` / `gettagorigin`, `level.spawnpoints`, `level.uavsettings`, shared funcs). A script
  error on that path leaves `hasradar` unset; the game's script error log of a built-map match would show it.
- Optional, for candidate 3: a map that is much smaller than `compassDetectionRange` * 1.41 gets a sweep that spends
  most of the period off the map. The build cannot change the dvar per map from data read here; a larger
  `minimap_corner` rectangle (more margin around the playable area) shortens the off-map part. Stock Shipment-size
  maps are also below 2121 units, so this is a look, not a fault (INFERRED).
- If a map is ever loaded without the DW package lookup, it needs a `mp/mapInfo.csv` row (cols 36-40 = 1, col 17
  empty); research 93 says a map zone cannot add one.

## 5. Not determined

- Whether the server copies `player.hasradar` to ps+756 (client-field table not read) and whether it did on the
  playtest map. This is the open question that decides candidate 1.
- The default of `cg_compassRadarSweepThickness` and `cg_compassMapRadarSweepThickness` (passed in xmm registers at
  0x1416D17BF and 0x141794C35; the loads were not traced).
- What sets cg+0x8271B (it is read in MLG / shoutcaster drawing code; probably a spectator flag) and what eFlag 0x1000
  means.
- The per-client snapshot step on the server (1.3) and whether it uses the map's visibility data.
- Which compass markers call `CG_Compass_GetMarkerRangeLimit` (virtual callers not traced).
- The Lua that calls `SetupMinimap` (whether any HUD passes `drawRadarSweep = false`).

IDA budget: about 36 read-only calls.

## 6. Which killstreak it was: the Personal Radar path (added 2026-11-14, main session)

Research 75 called the killstreak a "Personal UAV" and read `uav.gsc`. The owner's description ("no sweep or pings,
its model is low and invisible, it can be shot") fits the game's **Personal Radar** better: an escort drone, not the
team UAV. Its script is `cp_mp/killstreaks/helper_drone.gsc`
(`E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\`).

| What | Where | Finding | Label |
|---|---|---|---|
| Where the drone flies | `helperdrone_overwatchplayer` 1461-1513, `getcorrectheight` 1515-1522, `tracegroundpoint` 1524-1540 | Above the owner at ground height + 1500 + random(20). The ground is a sphere trace (radius 256) down from the drone's height + 2000, and never below `level.averagealliesz`. | VERIFIED |
| `level.averagealliesz` | `mp/killstreaks/helicopter.gsc:14-29` | The mean height of one class of spawn points, 0 when the map has none. Always defined. | VERIFIED |
| Why the model was invisible | research 121 | The built map's occlusion data ended at its geometry (top z 280 on that map). A drone 1,500 units above the ground was outside it, and the game hides a moving object there. The UAV proper orbits at about 4,800: outside as well. Fixed on 2026-11-14 (airspace). | INFERRED from 121 |
| "Flies very low" | same | 1,500 units above the ground is where the stock game puts this drone. Not a fault. | VERIFIED |
| The pings | `helperdrone_pingnearbyenemies` 1542-1591, started with the overwatch thread (`setoverwatchmodesettings` 1952-1958) | Every frame: each enemy within 1,200 units of the owner (2,500 on a large map) that is not pinged already gets `triggerportableradarping( enemy.origin, owner, 300, 3000 )`. Skipped: team mates, a player a shared "is alive" check rejects, a player who is still in the drone's 3-second pinged list, and a player with one named perk (a 15-letter `specialty_` name; Ghost fits). No map data is read. | VERIFIED |
| The sweep | - | This drone has no line sweep; the stock game draws the ping as a pulse (`CgCompassSystemMP::TriggerPortableRadarPing`, section 1.2). "No sweep" is the stock behaviour for it. | INFERRED |

So for the Personal Radar the script path is map-independent too, the missing model has a cause and a fix, and the
missing pings come down to the same candidates as section 3 (the enemies' perks first: a bot with the Ghost-like perk
is never pinged by this drone). If it is ever looked at in the game, the two facts that settle it are which
killstreak was used, and whether the bots' class had that perk.
