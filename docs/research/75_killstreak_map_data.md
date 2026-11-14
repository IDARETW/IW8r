# 75. Killstreak map data: Personal UAV and Chopper Gunner on a custom map

Scope: fault report on map `mp_iw8r_fix1` (MW2019, Replay 1.20), built by this editor. Symptoms:

- (a) Personal UAV: no radar sweep and no enemy pings on the minimap. The aircraft model is not visible in the sky, although it can be shot and flies very low.
- (b) Chopper Gunner: enemy players are invisible in the remote view (only their position markers show), though they take damage.
- (c) Cluster Strike works. Bots and the minimap image work.

Read-only research. Nothing was built, run or edited. Only this file was written. Scratch decode scripts are in the session scratchpad, not in the repo.

Status tags: **VERIFIED** = read in a file or checked by a decode in this pass. **INFERRED** = follows from code read, not run. **UNVERIFIED** = needs a game run. **not found** = searched, nothing located.

Sources used:

- Stock game scripts (read only, GSC source): `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\` (called `scripts/` below).
- Editor: `E:\IW8-Radiant\src\core\compile_iw8.cpp`, `map_script.cpp`, `gamemode_templates.cpp`.
- Converter (x64-zt): `E:\mw124\x64-zt\src\convert\iw3_iw8\` (`iw3_fastfile.cpp`, `iw3_scripts.cpp`, `iw3_render_assets.cpp`).
- Built output: `E:\IW8-Radiant\out\agent\mp_iw8r_fix1\iw8build\mp_iw8r_fix1\extraction\extracted\maps\mp\` (`.ents`, `replay-world.json`, `replay-collision.json`, generated `.gsc`).
- Translated map script: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\.iw3-scripts\mp_iw8r_fix1\mp_iw8r_fix1.gsc`.
- Stock entity dumps: `E:\IW8-Radiant\data\stock_dumps\mp_shipment\...` and `...\mp_backlot2\...`, decoded in this pass.

---

## 0. Answers in short

| # | Question | Short answer | Status |
|---|---|---|---|
| 1 | Map data the UAV needs | Two `script_origin` `minimap_corner` (present, at z 0). The UAV rig and aircraft are spawned by script. The aircraft height is set by the script, not by map bounds. The map never calls the stock `setupminimap`, so `level.mapcorners` and `level.mapsize` are never set. | VERIFIED (inputs), INFERRED (effect) |
| 1b | Could map bounds put the UAV "very low"? | No. The script puts the aircraft about 4,600 to 5,550 units above the rig (normal UAV), about 22,700 to 24,500 (advanced). The map bounds are not read for the height. The reported low flight is not explained by these scripts. | VERIFIED (arithmetic from code) |
| 2 | Map data that makes players visible in Chopper Gunner | None is read by `chopper_gunner.gsc`. Its thermal code only sets stock vision set names. The likely cause is engine side: the built map has one visibility cell (bounds z -32 to 280) and the Chopper Gunner camera sits about 1,500 units above the target and up to 9,000 units away, outside that cell. | UNVERIFIED |
| 3 | Stock vs built | Table in section 3. Built map lacks all killstreak helper entities except the two minimap corners. | VERIFIED |
| 4 | Editor additions, ranked | (1) emit the stock `setupminimap` call; (2) extend the world bounds up and out to cover the air volume, using a point origin; (3) set a sky image. Details in section 4. | INFERRED, needs a game test |
| 5 | Not determined | Section 5. | - |

---

## 1. What the Personal UAV reads

### 1.1 Inputs, this map, and stock

| Input | Read by | This map | Stock example | Status |
|---|---|---|---|---|
| Two `script_origin` with `targetname minimap_corner` | `scripts/cp_mp/killstreaks/uav.gsc:7-18` (`getentarray`, midpoint = rotation origin) | Yes, 2 entities at (-272,-272,0) and (272,272,0). Built `.ents`, lines near the end of the file. | shipment (1408,256,0) and (-2048,3712,0), both radius 200. backlot2 (3072,-3840,64) and (-2560,3584,64), no radius. | VERIFIED |
| UAV rig (`level.uavrig`) | Spawned by script at `uav.gsc:23-27`, model and yaw set there, spins with `rotateuavrig` | Not a map entity. Not needed in the map. | No map entity found for the rig in stock `.ents`. | VERIFIED |
| Rig origin | `uav.gsc:13-18`: midpoint of the two corners. If there are no corners, (0,0,0). | (0,0,0): midpoint of (-272,-272,0) and (272,272,0). | Corners at z 0 (shipment) and z 64 (backlot2). | VERIFIED |
| Aircraft height above the rig | `uav.gsc:398-442` (see 1.2) | Script-computed, no map input. | Same script path on stock. | VERIFIED |
| Map centre | `scripts/mp/spawnlogic.gsc:16, 1267-1273` (`findboxcenter` of spawn bounds). Read by remote UAV, gunship, cruise predator, not by `uav.gsc`. | Built from spawns at (+-192, +-192, 16). Approximately (0,0,8) if the spawn bounds start from (0,0,0) as the stock code does. | Derived the same way. | UNVERIFIED (exact value) |
| Map size and corners (`level.mapsize`, `level.mapcorners`) | Set only by `scripts/mp/compass.gsc` `setupminimap` (lines 103-104). Read by `mp/killstreaks/plane.gsc:249` and `cp_mp/killstreaks/chopper_support.gsc:1806-1807`. `uav.gsc` reads neither (count 0). | Never set (see 1.4). | Stock mp maps call `scripts\mp\compass::setupminimap` from their own `main()`, for example `mp/maps/mp_backlot2/mp_backlot2.gsc:14`. | VERIFIED |
| Airspace or strike height (`airstrikeheight`, z 2560 to 2576 in stock) | No reader found in `uav.gsc`. The stock name appears only in encrypted form in the scripts (see doc 40). | Absent. | 1 per map. | not found (reader) |
| Radar strength, team radar state | Engine builtins through `uav.gsc:847-886` (`_getradarstrength`, `updateteamuavstatus`). No map data. | Not map data. | - | VERIFIED |

Other note: the UAV code uses the stock `minimap_corner` lookup directly (`uav.gsc:7`), not `level.mapcorners`. So a missing `setupminimap` call does not stop the rig from being built.

### 1.2 Aircraft altitude and orbit (how the script places it)

From `scripts/cp_mp/killstreaks/uav.gsc`:

- `var_15` (height of the link offset): 6000 to 6500 (line 398). For the advanced and counter UAV types it is 30000 to 31000 (line 401).
- `var_25` (horizontal orbit radius): `randomint(1000) + 4000`, so 4000 to 4999 (lines 433, 438). For the advanced types, 24000 to 26000 (lines 435-436).
- Offset vector: normalised (cos, sin, height) times `var_15` (lines 441-442). The z part is `H^2 / sqrt(D^2 + H^2)`.
- Linked to the rig with that offset (line 455), `updateuavmodelvisibility` at line 456.

Result:

| UAV type | Height above rig (z) | Horizontal radius from rig |
|---|---|---|
| Normal (Personal UAV) | about 4,600 to 5,550 | 4,000 to 4,999 |
| Advanced and counter | about 22,700 to 24,500 | 24,000 to 26,000 |

Map bounds are not in this path. `uav.gsc:411` (a 6969 fallback z) and `uav.gsc:423-428` (`var_21`, `var_22` from the lowest spawn z and the rig z) compute values, but `var_22` is never read after line 428 in this function. So the script does not derive the height from the map bounds. The only map-derived input is the rig origin (from the corners).

Conclusion for "very low": the script puts the aircraft far above the 280-unit map top. "Very low" is not explained by this code. Cause not determined.

Note: `uav.gsc:446-451` adds 3000 to the offset in one game mode (a shared-function check, mode name encrypted, not found).

### 1.3 Visibility of the aircraft

- The aircraft starts hidden (`uav.gsc:631-650`, `updateuavmodelvisibility`). It is shown to enemies with `showtoplayer` on two level notifies. The owner is skipped in non-team modes.
- `uav.gsc:387-388` calls `scriptmoveroutline()` and `scriptmoverthermal()` on the aircraft.
- No map key is read here. The per-player show and hide is a script decision.

So "not visible in the sky" is not caused by a missing script call that a map key controls. The place to look is the engine's draw path (visibility cell, sky, or model), see section 2.3.

### 1.4 What the built map's script does for the minimap

Editor side:

- `src/core/map_script.cpp` (about line 344-345): the generated `main()` starts with the stock load call. The comment says x64-zt translates `maps\mp\_load::main` into `iw3compat_load_main`.
- The generated map script (`extracted\maps\mp\mp_iw8r_fix1.gsc`, 13,781 bytes) has no `setupminimap` or `setminimap` call.

Converter side:

- `x64-zt/src/convert/iw3_iw8/iw3_scripts.cpp:458-464`: the converted `main()` begins with `iw3compat_minimap(); iw3compat_generated();`.
- Translated script (`mp_iw8r_fix1.gsc`): `main()` calls `iw3compat_minimap()` at line 5 and `iw3compat_load_main()` at line 7. `iw3compat_minimap()` (lines 1962-1968) calls the raw `setminimap("compass_map_mp_iw8r_fix1", -272, 272, 272, -272, 1, ...)`.

Effect: the minimap image is drawn (matches the user report). `level.mapcorners`, `level.mapsize` and the corner angles from `compass.gsc:103-108` are not set on this map. The stock map scripts set them through `setupminimap`.

Also in the translated script (not the UAV, but related):

- `iw3compat_generated()` (lines 1921-1929) sets the converter's kill box for players at (-1296, -1296, -528) to (1296, 1296, 2176). This applies to players only (`iw3compat_out_of_bounds`, lines 518-545). It does not move the UAV aircraft.
- `iw3_scripts.cpp:62-70` (`DroppedBuiltins`) drops `setmapcenter` and `setteamradar` from IW3 map scripts. The stock UAV code calls builtins itself (for example `setteamradar` in `cp_mp/killstreaks/uav_cp.gsc:59`), so the drop does not touch it. No `setteamradar` call exists in this map's script.

### 1.5 Bounds of the built world

From `replay-world.json` (built):

- `bounds`: [-288, -288, -32] to [288, 288, 280]. That is 576 x 576 x 312 including the 16-unit pad. The brush geometry is about +-272 in XY. (Your note said about 512 x 512 x 280. The file says 576 x 576 x 312.)
- `sky`: `""` (empty). See 3 and section 2.
- `dpvs`: one cell. Its bounds equal the world bounds. `portals: []`. One `nodes` entry. Converter emits one conservative cell (doc 09, lines 45 and 167).
- Bounds are computed by the editor as the union of geometry, collision, and point origins, plus 16 (`compile_iw8.cpp:1703-1710`). So the cell top (z 280) comes from the highest geometry or point (the hall zone brush top 264, plus the pad).

---

## 2. What makes enemy players visible in Chopper Gunner

### 2.1 What the scripts do (`scripts/cp_mp/killstreaks/chopper_gunner.gsc`)

- Intro position (lines 237-260): `var_4` is (0, 0, z) where z = helipad origin z minus 1000 if a helipad entity is found (line 244), else 1500 (line 246). `var_6 = 9000` (line 248) is the horizontal distance behind the target, set to 7000 on one named map (line 251). The chopper goes to `var_0.origin - forward*var_6 + var_4` (line 253). So the camera is about 1,500 units above the target and about 9,000 units away (7,000 on that one map).
- Thermal: `choppergunner_handlethermalswitchinternal` (lines 819-862) sets the stock visionset `_encstr_AAF8...` (default), then cycles five stock names through `visionsetthermalforplayer` (line 843). It also calls `player_utility::setthermalvision(1, 12, 1000)` (lines 833, 849), which is `thermalvisionon` plus depth-of-field (`scripts/cp_mp/utility/player_utility.gsc:79-92`). No map key is read.
- Night branch: `isnightmap()` returns `level.nightmap` (`game_utility.gsc:137-140`). It is set only for a few maps (`game_utility.gsc:133`, `cp_so_highway`). Not set here, so the day branch runs.
- Outline: `chopper_gunner.gsc` has no outline call. Outline is on the UAV aircraft (`uav.gsc:387-388`) and on other killstreaks, not the Chopper Gunner players.
- Position markers: `chopper_gunner.gsc:720-732` (`targetmarkergroup`). This is why only markers show.
- Pilot mesh: `chopper_gunner.gsc:12` gets `heli_pilot_mesh` by `getlocaleent` (encrypted name). It is shown at line 691 and hidden at lines 1424-1425. Its absence is guarded, so it is not a failure cause.
- Helipad: the Chopper Gunner intro and Remote UAV both call `killstreak_utility::removeextracthelipad()` (`killstreak_utility.gsc:618-630`), whose name is encrypted. Not found on this map. Remote UAV reads its origin without a guard (`mp/killstreaks/remoteuav.gsc:151-152`), so a missing helipad would error there (not one of the reported symptoms).

### 2.2 Verdict

No per-map setting is read by the Chopper Gunner code path for enemy visibility. The script only picks stock vision set names and turns on thermal depth-of-field. The chain to check is engine side:

1. Visibility. The camera (about 1,500 above and about 9,000 away) is outside the only cell (z -32 to 280, XY +-288). The built map has one cell and no portals. Whether the engine then culls characters, or the world, is UNVERIFIED. Stock cells are not in the repo (see 3).
2. Vision asset. The build has no `vision\` folder. The editor writes `vision/<id>.vision` only when worldspawn `iw8r_film` is set (`compile_iw8.cpp:2876-2878`). Whether the stock thermal sets need a per-map vision file is UNVERIFIED.
3. Counter-check. Cluster Strike works. Its camera is also high. If that holds, the outside-the-cell idea is weaker, and the cause may be the thermal path for the character model. Not checked.

### 2.3 Why the bounds lever exists

`compile_iw8.cpp:1703-1710` makes the world bounds (and so the one DPVS cell, see `replay-world.json` `dpvs.cells[0].bounds` = `bounds`) the union of geometry, collision brushes, and point origins, plus 16. Any point entity placed high or wide enlarges the cell. This is the lever for section 4, item 2.

---

## 3. Stock vs built: killstreak and air data

Stock counts were decoded in this pass from `entityString` in `data/stock_dumps` (hex to text), with key ids resolved through `data/generated/iw8_canonical_keys.tsv`. Counts agree with `docs/research/42_stock_keys_killstreaks.md` section 5.1.

Built map: `E:\IW8-Radiant\out\agent\mp_iw8r_fix1\...\mp_iw8r_fix1.d3dbsp.ents`, 25 entities. Classes: worldspawn 1, info_player_start 1, mp_global_intermission 1, mp_dm_spawn_start 1, mp_tdm_spawn 1, mp_tdm_spawn_allies_start 1, mp_tdm_spawn_axis_start 1, script_model 5, script_origin 4 (room_center, tp_out, minimap_corner x2), script_brushmodel 2, trigger_multiple 5, trigger_use 1, trigger_hurt 1, fx_origin 1.

| Entity or setting | Stock example (count) | Built map | Present |
|---|---|---|---|
| `script_origin` `minimap_corner` | shipment 2 (radius 200), backlot2 2 (no radius) | 2 at z 0 | yes |
| `mp_global_intermission` | 1 per map | 1 at z 256 | yes |
| `script_struct_heli` `ks_heli_entrance`, `ks_heli_goal`, `ks_heli_path` | shipment 8 (4 + 4), backlot2 13 (4 + 4 + 5) | 0 | no |
| `script_origin` `airstrikeheight` | 1 per map (z 2576 shipment, z 2560 backlot2) | 0 | no |
| `script_origin` `heli_leave` | shipment 1 (10997 2604 2706) | 0 | no |
| `script_brushmodel` `heli_pilot_mesh` + noteworthy `heli_height_mesh` | 1 per map | 0 | no |
| `script_brushmodel` `care_package_col` | 1 per map | 0 in `.ents`. The converter's IW3 route adds one (`iw3_fastfile.cpp:10571-10632`, call site line 13030), but it is not in this build. | no |
| `script_brushmodel` `tactical_cover_col`, `tactical_ladder_col` | 1 each per map | 0 | no |
| `trigger_multiple` `white_phosphorus_damage` | 1 per map | 0 (converter route adds a `trigger_radius` of that name) | no |
| `script_struct` `nuke_expl_pos` | 1 per map | 0 | no |
| `script_model` `airdrop_crate` (and pallet, doc 42) | shipment 1 | 0 | no |
| World sky (`sky` image) | named per map, for example `sky_mp_backlot2` (doc 33) | `""` in `replay-world.json` | no |
| Stock map `main()` calls `scripts\mp\compass::setupminimap` | every stock mp map listed by grep (for example `mp_backlot2.gsc:14`) | not called | no |
| Stock `scripts\mp\load::main` | runs from `callback_startgametype` (`mp/gamelogic.gsc:2733-2735`) for every map | same (not map data) | yes |
| `vision/<map>.vision` | not in the repo for stock | none in build (no `iw8r_film`) | no (stock UNVERIFIED) |
| DPVS cells | not in the repo for stock | 1 cell, no portals | stock UNVERIFIED |
| `footprint_mask_group default` | stock on every carrier (doc 42) | on script_model, not on the intermission or origins | partial, minor |
| worldspawn `northyaw` | 90 on shipment (decoded) | 90 | yes |
| Helipad entity used by Chopper Gunner and Remote UAV | name encrypted, not identified | none | no (stock UNVERIFIED) |

Note on the stock load call: stock maps call `scripts\mp\load::main()` from their own main (for example `mp_backlot2.gsc:11`), and the engine calls it from `callback_startgametype` (`gamelogic.gsc:2735`). `mp/load.gsc:56` sets `visionsetthermal(...)` there. So the thermal default is set for every map, not by the map script.

---

## 4. Ranked editor additions

Each item says what the game reads, and what it should fix. None is proven in game.

### 4.1 For (a) Personal UAV

1. Emit the stock compass setup in the generated map script: `scripts\mp\compass::setupminimap("compass_map_<id>", "compass_map_<id>")` (the two-argument form stock maps use, for example `mp_backlot2.gsc:14`). Put it in the generated `main()` (`map_script.cpp`), in place of relying on the raw `setminimap` only.
   - What the game reads: `compass.gsc:103-104` writes `level.mapsize` and `level.mapcorners`, and `compass.gsc:113` calls the same `setminimap`. `plane.gsc:249` and `chopper_support.gsc:1807` read `level.mapsize`.
   - Caveat: `uav.gsc` reads neither variable (count 0). This is a stock-parity fix, not a proven UAV fix. Also check that the converter passes the `scripts\mp\compass::` call through (not checked).
2. Make the world bounds reach the aircraft volume. Add an invisible point `script_origin` (a non-killstreak name, for example `airspace_probe`) high above the map, so the union in `compile_iw8.cpp:1703-1710` raises the bounds and the one DPVS cell. The UAV aircraft sits about 4,600 to 5,550 above the rig at (0,0,0), and within about 5,000 horizontally.
   - What the game reads: the single cell is the world bounds (`replay-world.json` `dpvs.cells[0].bounds`). Doc 09 (lines 45, 167) says the converter emits one conservative cell.
   - Caveat: this enlarges the sun shadow range, light grid and play space, and the cell gets large. It is a test, not a fix. Whether a tool brush also counts is UNVERIFIED.
3. Set a sky: worldspawn `sky` (or `skyname`) with a sky image, so `world.sky` is not empty. Without it the converter writes a dark placeholder cube (`iw3_render_assets.cpp:6189-6193`).
   - What the game reads: `world.sky` (doc 33). Relevance to the aircraft being invisible in the sky is LOW confidence.

### 4.2 For (b) Chopper Gunner enemy visibility

1. The same bounds change as 4.1 item 2 (the Chopper Gunner camera is about 1,500 above and about 9,000 away). This is the only candidate found that a map-side edit can change.
2. Per-map vision: write a `vision/<id>.vision` (from `iw8r_film`, `compile_iw8.cpp:2876-2878`) or check whether the stock thermal sets need one. UNVERIFIED that it matters.
3. Helipad for altitude parity: a Chopper Gunner intro uses helipad z minus 1000, else 1500 (`chopper_gunner.gsc:243-246`). Adding the stock helipad needs its name, which is encrypted and not found. Not a visibility fix by itself.

### 4.3 Stock-parity entities (no proven effect on the reported faults)

- `airstrikeheight` (`script_origin`, z 2560), `heli_pilot_mesh` plus `heli_height_mesh` (`script_brushmodel`), `care_package_col`, `tactical_cover_col`. The converter adds some of these (`iw3_fastfile.cpp:10571-10632`). The build does not show them, so confirm which converter binary built this map.
- `ks_heli_entrance` chain (`script_struct_heli`). The Chopper Gunner reads `level.heli_structs_entrances` (`chopper_gunner.gsc:257`). The only setter found is the Cp skydive path (`cp/utility.gsc:9863-9874`), not MP. So for MP the chain is probably not read by this route (not confirmed).

---

## 5. Not determined

- Why the Personal UAV aircraft is not visible and flies low in game. The script places it about 4,600 to 5,550 above the rig. No map key in this build changes that.
- Whether the Chopper Gunner enemy invisibility is the visibility cell, a missing per-map vision asset, or the character thermal pass. No stock DPVS or vision data is in the repo to compare. Not run.
- Encrypted names (not decoded): the UAV rig model and tag, the helipad name, `airstrikeheight`, `heli_pilot_mesh`, the Chopper Gunner thermal sets, and the game-mode name in `uav.gsc:446-451`. Their readers are known from code, but the strings are not.
- Whether the build's converter contains `AddReplayTemplateEntities` (`iw3_fastfile.cpp:10571`, call at line 13030). The source is newer than the build, but the built `.ents` has none of its three entities, so either the route or the binary differs. Not determined.
- Whether `scripts\mp\compass::setupminimap` survives the editor-to-converter translation. Not checked.
- The stock DPVS cell layout and stock sky image per map (only the names in doc 33 are known). Not in the repo.
- The exact spawn-bounds start value used for the map centre (approximately (0,0,8)). Not verified in the engine init path.
- Why the built script's player kill box is (+-1296, +-1296, -528 to 2176) rather than derived from the world. Not related to the aircraft, so not traced further.
- Remote UAV on this map: the helipad read at `remoteuav.gsc:151-152` has no guard. Not one of the reported symptoms, not run.
