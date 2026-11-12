# 49. Match cameras: intro, infil, intermission, killcam and spectator points (roadmap 6.19)

Status: research only. Nothing was built, run or written except this file. No game file was run. The stock entity
dump was parsed with a scratch script (`C:\Users\user\AppData\Local\Temp\claude\2b25dc13d65a86cc\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\parse49.py`, plus `chain49.py` and `keys49.py` in the same folder). Everything that depends on the running game is marked **UNVERIFIED**.

## 0. Sources and caveats

- **Stock entities, mp_shipment**: `E:\IW8-Radiant\out\shipment_named.ents` (611 blocks, decoded with `data\generated\iw8_canonical_keys.tsv`). Parsed directly in this task.
- **Stock entities, mp_backlot2**: no decoded `.ents` file on disk. Counts and values come from `docs\research\10_iw8_entity_inventory.md` and `docs\research\42_stock_keys_killstreaks.md` (not re-parsed). Marked where used.
- **GSC**: the IW8 8.19 dump, `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts` (cited as `mp/...` relative to that folder). Its README says it is from "IW8 8.19 build win64 Apr 18 2020", not Replay 1.20. Every GSC citation is therefore to 8.19; 1.20 parity is UNVERIFIED. Most identifiers are `_encstr_<hex>`. Names below are inferred from doc 40 section 6 or from the context of use; the inference is stated each time.
- **Converter**: `E:\mw124\x64-zt\src\games\iw8\dump\maps.cpp` (read only).
- **Editor**: `E:\IW8-Radiant\src` (read only). Entity defs in `E:\IW8-Radiant\data\iw8_entity_defs.json`.
- **Not reachable from the repo**: the 1.20 GSC (`common_base_mp` decompile), the backlot2 entity string on disk, and the x64-zt key table for `_encstr_` hashes. Any `_encstr_` value marked "inferred" is a guess until decoded.

## 1. Answer 1: what stock MP maps use, with keys and counts

Pre-match, intermission, infil, killcam and spectator roles, by entity. "Shipment" is parsed from the `.ents` dump; "Backlot2" is from docs 10 and 42.

| Role | Entity (classname, keys) | mp_shipment | mp_backlot2 | Read by (GSC, 8.19) |
|---|---|---|---|---|
| Pre-match / spawn-selection camera (the "intro") | `script_struct`, `targetname camera_intro`, keys `_color 0 .5 0`, `target pf1_auto3126`, `angles 6.99927 44.9996 -4.2e-07`, origin `-1897 792 658` | 1 | 1 (doc 40 total of 2) | `mp/spawncamera.gsc:32` (getstructarray by targetname) |
| Camera path after the intro camera | `script_struct`, `targetname pf1_auto3126`, origin `872.5 1619.5 420`, angles `4.29081 164.183 -0.362`, no `target` | 1 (2-node chain, end) | not parsed | **Not read** by `spawncamera.gsc` (no `.target` use there). Role UNVERIFIED. |
| Intermission (end-of-round) camera and the spectator point | `mp_global_intermission`, origin `-864 1584 216`, angles `0 18 0`, keys footprint_mask_group only | 1 | 1 (doc 42 row gives `678 126 526`; which value is backlot2 was not re-checked) | `mp/playerlogic.gsc:1944-1946` (intermission spawn), `mp/playerlogic.gsc:1844-1907` (`getspectatepoint`) |
| Infil aircraft / van start (infil structs) | `script_struct_infil_van_hackney_alpha` and `script_struct_infil_lbravo_alpha`, keys `script_noteworthy infil_van_hackney` / `infil_lbravo`, `name`, `script_team`, `targetname mp_infil` (doc 10) | 0 | 1 each (2) | `mp/infilexfil/infilexfil.gsc:272-274` (`get_all_infils`), `:287-300` (`infil_is_gamemode`) |
| Infil animation export volume | `export_volume`, `target tag_align_mp_infil`, `filterpreset Anim_Export_Volumes`, `volume_intersection_test BOUNDING_BOX` | 0 | 1 | Not a gameplay read in the code I read. Animation export only (doc 42 section 5.2) |
| Infil door models | `script_model`, noteworthy/targetnames `infil_door_left`, `infil_door_right`, `infil_door_chain` | 0 | 1 row, 3 names; per-map count UNVERIFIED | `mp/infilexfil/lbravo_infil.gsc:493` etc. (`getent`) |
| Minimap corners (camera and compass bounds) | `script_origin`, `targetname minimap_corner`, keys `radius`, origin | 2 | 2 | `mp/compass.gsc:12` (read), `:104` (`level.mapcorners`); `mp/spawncamera.gsc:104-107, 126, 130` (used) |
| Killcam fallback camera | none. No map entity with `killcamentity` or `killcamentitylookat` (key ids 558 and 559) in the shipment dump. The stock killcam code makes no `getent` call. | 0 | 0 (doc 10 lists no killcam class) | Engine-side. `mp/killcam.gsc:134-156` (style selection by entity number), `mp/final_killcam.gsc:241, 305` (entity numbers). Cinematic paths in `mp/cinematic_camera/*.txt` |
| Caster / MLG spectator cameras (not entities) | CSV rows, not entities: `mp/camerapositions.csv` and `mp/camerapositions_<gametype>.csv` | **no row** | **no row** | `mp/spectating.gsc:~25-67` (`setlevelmlgcam`, `getlevelmlgcams`); the CSV key is the map name (`_encstr_9570...` dvar, read in `common/utility.gsc:1743`; name inferred) |
| Pre-match infil camera (infil gametype) | none. Computed from `getmissedinfilcamerapositions(team)` in `arm.gsc:3369` | 0 | 0 | `mp/playerlogic.gsc:379-395` |

Counts over both zones: `mp_global_intermission` 2, `camera_intro` 2 (doc 40), `script_struct_infil_*` 2, `export_volume` 1, `minimap_corner` 4. No stock map has a killcam entity or a spectator entity other than `mp_global_intermission`.

Key-usage check (shipment only): none of the 51 keys used in `mp_shipment` match `camera`, `killcam`, `spectat`, `intermission`, `intro`, `infil`, `exfil` or `outro` (script `keys49.py`). `intermission` (id 522) and `spectator` (id 928) are in the key table but unused in this map.

## 2. Answer 2: what the converter and the editor keep

| Class | x64-zt converter `isReplayEntity` (`maps.cpp`) | Editor `acceptedPointClass` (`compile_iw8.cpp:492-508`) | Defs `buildAccepted` | Editor template today |
|---|---|---|---|---|
| `mp_global_intermission` | kept (`maps.cpp:187`) | accepted (`:494`) | true | none |
| `script_struct` (camera_intro, pfN path, others) | kept (`maps.cpp:203`) | accepted (`:497`) | true | none |
| `script_origin` (minimap_corner) | kept (`script_origin` in the list) | accepted (`:497`) | true | none (editor writes 2 corners, doc 40) |
| `script_struct_heli` | kept, "shipped" list (`maps.cpp:244`) | accepted (`:503`) | true | none (killstreak, doc 42) |
| `script_struct_infil_van_hackney_alpha`, `script_struct_infil_lbravo_alpha` | **dropped** (not in any list) | **refused** (not in list); reported in `unacceptedClasses` (`compile_iw8.cpp:1549-1552`) | false | none |
| `export_volume` | dropped | refused | false | none |
| `reflection_probe` | not in list (handled as world probes) | skipped at `compile_iw8.cpp:1544` (world.json probes) | false | none |
| `script_struct_mp_rugby_endzone`, `script_struct_mp_rugby_jugg_crate`, `trigger_multiple_mp_rugby_endzone` | dropped | refused | false | none |
| `collmap_scriptable` | dropped | refused | false | none |

What the converter does with a dropped class: `maps.cpp:610-622` (count into `unsupported`, no build error). The editor logs it and leaves it out, but does not warn in Check Map (`compile_iw8.cpp:1549-1552`). That is a silent drop for the user.

The closed list in `maps.cpp:179-216` is the one to change for any new class. The comment there says why it is closed (an unknown class can carry state Replay does not understand). This is the reason infil cannot be added without a converter change (F3 in doc 40).

## 3. Answer 3: minimum for a custom map, with GSC evidence

### 3.1 Intermission and spectator points (required)

- `mp_global_intermission` is the only entity that the end-of-round and spectator code reads (`mp/playerlogic.gsc:1944-1946`: `getentarray(<B4D3...>, <AC11...>)` then `checkdynamicspawns` then `var_0 = var_3[0]`). `AC11...` is `classname` (doc 40 section 6, `AC110A7F`). `B4D3...` is the classname string, **inferred** from use (the same lookup appears in `cp/cp_globallogic.gsc:1296` and the entity exists in both stock maps; the encrypted string itself is not decoded in the repo, **UNVERIFIED**).
- Spectator points: `mp/playerlogic.gsc:1844-1907` (`getspectatepoint`) uses the same list. With more than one, entities whose `script_noteworthy` contains the token `_encstr_82E0...` are deleted (`:1881-1887`, `:1895-1901`); the token meaning is unknown (**UNVERIFIED**). With exactly one, it is used as is (`:1905`); the pick is `spawnlogic::getspawnpoint_random` (`:1907`).
- With no `mp_global_intermission`, `var_3[0]` is undefined and `var_0.origin` at `mp/playerlogic.gsc:1954` (`self spawn( var_0.origin, ... )`) is read on undefined. This is code-evident, so **runtime error UNVERIFIED** (expected: a script error at match end).
- The editor already requires one: `src/core/gameplay_checks.cpp:149` (`missing mp_global_intermission`).

### 3.2 Pre-match / spawn-selection camera (the "intro")

- Shown by `startspawncamera` (`mp/spawncamera.gsc:214-265`), called from `mp/playerlogic.gsc:392` (after a death) and `mp/spawnselection.gsc:922` (spawn selection). The camera entity `spawncameraent` is created in the death-spectate code near `mp/spawncamera.gsc:196-206`. If it is missing, the flash plays and nothing else happens (`:221-224`).
- Camera source: `getspawncamera` (`mp/spawncamera.gsc:283-440`). The default is `level.currentcamera[team]`, which starts as the key `_encstr_8B16...` (`:30`). That key is **inferred to be "default"** (UNVERIFIED).
- Stock camera lookup: `getstructarray(<9AF4...>, targetname)` at `mp/spawncamera.gsc:32`. `9AF4...` is **inferred to be `camera_intro`**: it is the only struct of that key in the stock dump, and it is the value compared to `script_label` at `:44`. (MEDIUM confidence.)
- Keying: for each struct, `var_6 = script_label`, else `script_noteworthy` (`:40-45`); team from `script_team` (`:58`). Team `_encstr_AF19...` means all teams (`:60-66`, **inferred to be "all"**). The result is `level.spawncameras[label][team]` (`:75`).
- **Problem found**: the stock `camera_intro` has no `script_label`, no `script_noteworthy` and no `script_team` in the dump (keys: `_color`, `target`, `targetname`). Under the 8.19 code it would be stored under an undefined key (`:75`), and the `default` key would then be created at the world origin by `createdefaultcameras` (`:81-121`). So either 1.20 differs from 8.19, or the stock intro does not come from this struct. **UNVERIFIED, and this is the main open question for 6.19.**
- The camera struct's `target` chain (`pf1_auto3126`) is not followed in `spawncamera.gsc` (grep for `.target` returns nothing). Its role is **UNVERIFIED**.
- `createdefaultcameras` (`:81-121`) builds origin-zero cameras when no default exists. `:124-126` (`getstartspawnavg` for allies and axis starts, then `level.mapcorners[0]` with no guard) and `:130` need two map corners. **So a custom map needs two `minimap_corner` script_origins and allied and axis start spawns** or the spawn-camera init errors (`mp/compass.gsc:12, 104`; `spawncamera.gsc:126,130`).
- `getstartspawnavg` (`mp/spawncamera.gsc:1100-1115`) averages spawns found by a name built from two `_encstr_` parts, which the code concatenates (**inferred**: `mp_<mode>_spawn_<team>_start`). TDM allied and axis starts already satisfy this (`gameplay_checks.cpp`, TDM rule).

### 3.3 Infil sequences (not for custom maps)

- Infil uses structs found by `script_noteworthy` (`get_all_infils`, `mp/infilexfil/infilexfil.gsc:15-21`, `:272-274`). A struct only runs if `infil_is_gamemode` passes: `spawnflags` must be set, `level.teambased` must be true, and per-gametype rules apply (`:287-300`).
- Map-specific scripts exist for backlot2 only (`mp/infilexfil/van_hackney_infil.gsc`, `lbravo_infil.gsc`, `rappel_hackney_infil.gsc`, etc.). A custom map cannot reuse them without the backlot2 assets and scripts.
- Stock infil structs in the dump show no `spawnflags` key, but doc 10 lists only the top 8 keys per class, so this is **UNVERIFIED** (backlot2 not re-parsed).
- Recommendation: do not template infil for 6.19. Keep it refused and warn (see 4.C).

### 3.4 Killcam and outro

- No stock map entity is read by the killcam code. `mp/killcam.gsc` and `mp/final_killcam.gsc` use entity numbers and cinematic files (`mp/cinematic_camera/killcam.txt`, `final_kc_*.txt`). A custom map needs nothing for killcams.
- The outro is the final killcam plus the intermission (`mp/playerlogic.gsc:1911` via `mp/gamelogic.gsc:4041, 4813`). Only the intermission entity matters.

### 3.5 Minimum set (custom map)

| Need | Entity | Required? | Evidence |
|---|---|---|---|
| End-of-round camera and spectator point | `mp_global_intermission`, exactly one | **Required** (check exists, `gameplay_checks.cpp:149`; `playerlogic.gsc:1946`) | 3.1 |
| Two minimap corners | `script_origin` `targetname minimap_corner` ×2 | **Required** (`compass.gsc:12, 104`; `spawncamera.gsc:126,130`) | 3.2 |
| Allied and axis starts | `mp_tdm_spawn_allies_start`, `mp_tdm_spawn_axis_start` | **Required** (already checked) | 3.2 |
| Pre-match / spawn camera | `script_struct` `targetname camera_intro` + keying (`script_label`, `script_team`) | Recommended. Keying **UNVERIFIED** | 3.2 |
| Camera path | `pfN_auto` chain | Not read by the code I read | 3.2 |
| Killcam | none | Not needed | 3.4 |
| Infil | none | Not for custom maps | 3.3 |

## 4. Answer 4: editor additions

### 4.A Entity defs (`data/iw8_entity_defs.json`)

- `mp_global_intermission` (line 1550): description says "inferred from stock values". Change to cite `playerlogic.gsc:1944-1946` and the check rule. Add `angles` note: the yaw is the only value stock uses (already in defs).
- `script_struct` (line 2988): add keys `script_label` and `script_team` with the 8.19 meanings above (**UNVERIFIED** keying), and `target` for the camera path. Add `camera_intro` as a documented targetname example.
- Keep `script_struct_infil_*`, `export_volume` as `buildAccepted: false`. Give them a note "converter drops; see Check Map warning".

### 4.B Template "Primitives > Game Modes > Match Cameras"

- Add to `buildTemplates()` in `src/core/gamemode_templates.cpp` (starts at line 37; the spawn sets are around line 148-171; add before the `return out;`). Menu wiring is automatic: `src/qtapp/main_window.cpp:1835` builds "Game &Modes" from `gameModeTemplates()`.
- Template `id = "match_cameras"`, name `Match Cameras`, one line description: "Intermission camera and the pre-match camera struct; add the minimap corners separately if the map has none."
- Entities (offsets from the placement point; keys verbatim):
  1. `mp_global_intermission`, offset `(0, 0, 64)`, keys `angles "0 0 0"`, `footprint_mask_group "default"`.
  2. `script_struct`, targetname `camera_intro`, offset `(-256, 0, 96)`, keys `angles "0 0 0"`, `script_label "default"` (**UNVERIFIED**), `script_team "all"` (**UNVERIFIED**), `target "camera_intro_path"`, `_color "0 .5 0"`.
  3. `script_struct`, targetname `camera_intro_path`, offset `(256, 0, 128)`, keys `angles "0 180 0"`. Role UNVERIFIED (not read by the 8.19 code).
- Do not add minimap corners to the template. Check them instead (4.C).
- Template ID must match the existing pattern `GameModeTemplate_<id>` (`main_window.cpp:1837`).

### 4.C Checks (`src/core/gameplay_checks.cpp`, near line 149)

| Check | Level | Rule and reason |
|---|---|---|
| `mp_global_intermission` count 0 | error (exists, line 149) | Keep. |
| `mp_global_intermission` count > 1 | warn | Spectator pick is random and the noteworthy filter is unknown (`playerlogic.gsc:1880-1907`). |
| intermission angles pitch or roll not 0 | warn | Same as spawn rule (`gameplay_checks.cpp:124-131`). |
| intermission within 32 units of a spawn | warn | Same as spawn spacing rule. |
| intermission with no floor below | warn | Reuse `hasFloor` (128 units). |
| `minimap_corner` count != 2 | error | `compass.gsc:104` and `spawncamera.gsc:126,130` need exactly two corners. |
| `camera_intro` count 0 | warn | Falls back to origin-zero camera (`spawncamera.gsc:81-121`). |
| `camera_intro` with no `script_label` or `script_noteworthy` | warn | Keying **UNVERIFIED**; may be ignored (`:40-75`). |
| `camera_intro` `target` names no entity | warn | Chain broken; role UNVERIFIED. |
| `script_struct_infil_*`, `export_volume`, `script_struct_mp_rugby_*`, `collmap_scriptable`, `trigger_multiple_mp_rugby_endzone` present | warn | Dropped by the converter (`maps.cpp:179-216`) and currently silent (`compile_iw8.cpp:1549-1552`). |

### 4.D Tests

- Add a case next to the CTF template test in `src/qtapp/qt_gui_test.cpp:1298-1320`: insert `match_cameras`, check the entity count goes up by the template size, and that one undo restores it. Assert the intermission entity count is 1 after insert.

### 4.E What not to change for 6.19

- Converter (`maps.cpp:179-216`) and editor list (`compile_iw8.cpp:492-508`): no change for intermission, camera and minimap. Infil classes stay refused until F3 (doc 40) is done and verified in game.

## 5. Open questions (UNVERIFIED)

1. The key `_encstr_9AF4...` = `camera_intro`, and `_encstr_8B16...` = "default", `_encstr_AF19...` = "all". Inferred from use only. Decode from the 1.20 GSC or the x64-zt key table.
2. The stock `camera_intro` has no label, noteworthy or team keys, so the 8.19 keying would drop it. Is the 1.20 keying different, or is the intro read from elsewhere?
3. What `camera_intro`'s `target` chain (`pf1_auto3126`) feeds, if anything.
4. Team names used in `level.teamnamelist` (likely allies and axis).
5. Meaning of the noteworthy token `_encstr_82E0...` that removes intermission points from the spectator pool.
6. Whether `mp/camerapositions*.csv` (caster cameras) is used outside MLG mode, and whether a custom map needs rows there.
7. mp_backlot2 `camera_intro` position and the backlot2 intermission origin (doc 42 gives `678 126 526`; order not re-checked).
8. Whether infil structs in backlot2 carry `spawnflags` (dump not re-parsed).

## 6. Citation index

- Stock dump: `E:\IW8-Radiant\out\shipment_named.ents`; keys `E:\IW8-Radiant\data\generated\iw8_canonical_keys.tsv`.
- Docs: `E:\IW8-Radiant\docs\research\10_iw8_entity_inventory.md`, `40_stock_scripted_objects.md` (section 6), `42_stock_keys_killstreaks.md` (section 5.1-5.2).
- GSC (8.19, read only): `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\mp\` `spawncamera.gsc`, `playerlogic.gsc`, `killcam.gsc`, `final_killcam.gsc`, `spectating.gsc`, `compass.gsc`, `infilexfil\infilexfil.gsc`; `scripts\cp\cp_globallogic.gsc:1294-1297`.
- Converter (read only): `E:\mw124\x64-zt\src\games\iw8\dump\maps.cpp` (179-216 class lists, 244 `script_struct_heli`, 610-622 unsupported).
- Editor (read only): `E:\IW8-Radiant\src\core\compile_iw8.cpp` (492-508, 1544-1552), `src\core\gameplay_checks.cpp` (124-131, 149), `src\core\gamemode_templates.cpp` (37, 148-171), `src\qtapp\main_window.cpp` (1835-1845), `src\qtapp\qt_gui_test.cpp` (1298-1320), `data\iw8_entity_defs.json` (1550, 2988).
- Roadmap: `E:\IW8-Radiant\docs\ROADMAP.md` line 272 (6.19, "open", M, R).
