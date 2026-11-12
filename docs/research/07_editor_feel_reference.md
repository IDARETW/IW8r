# CoD4 Radiant "editor feel" reference (for IW8 editor parity)

Scope: everyday conventions that CoD mappers expect, taken from the iw3xo-radiant source and the stock CoD4 Radiant command table. Read-only research; no executables were run.

## Evidence key and conventions

- `src/...:N` paths are relative to `E:\IW8-Radiant\reference\iw3xo-radiant\`.
- `HK` = `assets/bin/IW3xRadiant/hotkeys.ini` (the shipped iw3xo default). The installed copies at `E:\CoD4\game_build\Call of Duty 4 Modern Warfare\bin\IW3xRadiant\hotkeys.ini` and `E:\CoD4\radiant\bin\IW3xRadiant\hotkeys.ini` are byte-identical to it (verified with `diff`).
- "Stock table" = the 187-entry `g_Commands` array at VA `0x73B240` (`src/game/functions.cpp:78-79`), read statically from `E:\CoD4\game_build\...\bin\IW3xRadiant.exe` by a read-only PE parser (scratchpad `dump_cmds.py`). Both installed `IW3xRadiant.exe` copies produce identical tables. The stock table is what the editor uses when no `hotkeys.ini` exists; with an ini present, the ini overrides it at load (`src/ggui/hotkeys.cpp:382-467`).
- Modifier bits in the table: 1 = Shift, 2 = Alt, 4 = Ctrl (`src/ggui/hotkeys.cpp:375-379`).
- Anything I could not pin to a source line or a binary table is marked **UNKNOWN**. Logic that lives only in the stock CoD4 binary (for example `CCamWnd::Cam_MouseControl` at `0x403950`) is not in the source tree, so its exact feel is UNKNOWN unless a source comment or UI string says otherwise.
- External check: the Zeroy wiki CoD4 Radiant page (https://wiki.zeroy.com/index.php/Call_of_Duty_4:_Radiant) confirms the example camera binds `CameraForward=UP`, `CameraBack=DOWN`, `CameraLeft=LEFT`, `CameraRight=RIGHT`, `CameraUp=D`, and that Preferences is `P`. It does not document mouse camera controls, grid, snap or undo. The Radiant Command List it points to was not retrievable, so no third-party key list was used for cross-checking.

---

## 1. Camera

### 1a. Free-fly keys (keyboard)

Bindings are identical in the stock table and in `HK` (section 4 has the full list):

| Action | Key | Source |
|---|---|---|
| Forward / back | Up / Down | stock table `CameraForward`, `CameraBack`; `HK:43-44` |
| Left / right | Left / Right | stock `CameraLeft`, `CameraRight`; `HK:45-46` |
| Strafe left / right | Comma / Period | stock `CameraStrafeLeft/Right`; `HK:53-54` |
| Up / down (height) | D / C | stock `CameraUp/Down`; `HK:49-50`; wiki example confirms `CameraUp=D` |
| Pitch up / down | A / Z | stock `CameraAngleUp/Down`; `HK:51-52` |

- The behaviour of Left/Right (turn versus strafe) and the key acceleration curve are **UNKNOWN**. The key handler lives in the stock binary; the source only routes key events (`src/common/mainframe.cpp:620-690`, `on_keydown_intercept`).
- While the PhysX character controller is active, camera keys are suppressed in the camera window (`src/common/mainframe.cpp:~640`, `!m_character_controller_enabled`).

### 1b. Mouse (camera window, 3D view)

Button state is `ccamwnd::m_nCambuttonstate` (`src/common/camwnd.hpp:31`). The source only compares three values, which tells us the combinations that matter (`src/ggui/camera.cpp:1500-1504`):

| State value | Combination | Evidence |
|---|---|---|
| 2 | Right mouse alone | `camera.cpp:1504` (exempt from cursor-restore) |
| 10 | Ctrl + RMB | `camera.cpp:1502` comment |
| 14 | Ctrl + Shift + RMB | `camera.cpp:1500` comment |

Bit values are inferred from these three numbers (2 = RMB, 8 = Ctrl, 4 = Shift). The bit layout is **inferred**, not stated in source.

- **Ctrl + Shift + RMB: rotate (pitch and yaw).** `cam_rotate()` adjusts `angles[1]` and `angles[0]` from mouse delta and recentres the cursor (`src/common/camwnd.cpp:22-46`, comment on line 22 reads "CTRL + SHIFT + RMButton"). The hook is installed at `camwnd.cpp:431` (`0x405346`).
- **Ctrl + RMB: drag to move forward/back and yaw.** `cam_positiondrag()` moves `origin` along the view vector using vertical mouse delta and yaws with horizontal delta (`src/common/camwnd.cpp:48-87`, hook at line 432, `0x405321`). The button combination is inferred from the 10 check and this function's behaviour.
- **Plain RMB drag (look)**: the stock handler is in the binary (`CCamWnd::Cam_MouseMoved` at `0x404FC0`, `camwnd.cpp:118-133`). Its exact mapping is **UNKNOWN** from the source. The model-preview help text documents a similar "Right Mouse = rotate, Right Mouse + Ctrl = rotate modifier, Right Mouse + Ctrl + Shift = move" convention, but that is the model selector, not the main camera (`src/ggui/modelselector.cpp:466-468`).
- Left-button camera drag and middle-button camera behaviour: **UNKNOWN** (`camwnd.cpp:161-211` only forwards to other windows).
- Camera scroll wheel: forwarded to the camera-scroll routine `0x4248A0` with direction taken from wheel sign (`src/common/mainframe.cpp:~520-540`). Zoom-in direction is `scroll_dir = zDelta <= 0 ? 1 : -1`.

### 1c. Speeds and acceleration

| Setting | Where | Range | Default | Source |
|---|---|---|---|---|
| Camera speed (`m_nMoveSpeed`) | Preferences > Camera > Mouse | 10 to 5000 | UNKNOWN | `src/ggui/preferences.cpp:242`, `afx.hpp:1431` |
| Camera angle speed (`m_nAngleSpeed`) | Preferences > Camera > Mouse | 1 to 1000 | UNKNOWN | `preferences.cpp:243`, `afx.hpp:1430` |
| Camera angle speed (PhysX movement) | Preferences > Camera | 10 to 1000 | UNKNOWN (installed `dvars.cfg` shows `physx_camera_sensitivity "80"`, which is a saved value, not a default) | `preferences.cpp:244-247` |
| Field of view (`camera_fov`) | Preferences > Camera | 10 to 160 | UNKNOWN | `preferences.cpp:235-238`, `afx.hpp:1424` |
| Camera mode (`camera_mode`) | Preferences > Camera, and toolbar toggle | 0 to 2 | UNKNOWN | `preferences.cpp:233`, `toolbar.cpp:521-534` |

Scaling used in the code:

- Look rotation: `angle += m_nAngleSpeed / 500 * pixel_delta` (`camwnd.cpp:34-35`, `:59`).
- Mouse drag move: `delta = m_nMoveSpeed / -250 * pixel_delta_y` along the view vector (`camwnd.cpp:64-68`).
- Acceleration for keyboard free-fly: **UNKNOWN** (stock binary). The `accelerate()` code in `src/common/physx/cct_camera.cpp:1021-1044` (stopspeed 100) belongs to the PhysX character controller, not free-fly.

### 1d. Orbit, centre on selection, 2D view

- **Centre camera on selection**: `center_camera_on_selection`, default hotkey End (`HK:202`), added by an addon in `src/common/camwnd.cpp:458-482`. For a non-worldspawn entity it moves the camera to the entity origin. For worldspawn brushes it moves to the brush centre (`mins + abs((maxs - mins) * 0.5)`, which equals the centre). It does not orbit; it recentres the position.
- **Orbit around selection**: no orbit mode found in source. **UNKNOWN / not present** in the source.
- **Centre 2D on camera**: `Center2DOnCamera`, Alt+X (stock and `HK:173`). Menu item at `src/ggui/menubar.cpp:445`.
- **Centre view (2D)**: `CenterView` (stock End, `HK:74` is Ctrl+End in the ini). Menu at `menubar.cpp:441`.
- **2D grid zoom**: wheel zoom in the grid window, clamped to a maximum scale of 160 (`src/common/cxywnd.cpp:382-384`). Zoom towards cursor is controlled by `grid_zoom_to_cursor`, default **true** (`cxywnd.cpp:717-721`). Zoom In/Out hotkeys are Delete / Insert (`HK:75-76`). Ctrl+Insert and Ctrl+Delete are `ZZoomOut`/`ZZoomIn`.
- **2D pan**: middle-button drag mapping is in the binary; source only forwards (`cxywnd.cpp:264-272`). Pan behaviour: **UNKNOWN**. The "Fast view dragging" option hides static models while moving the grid (`preferences.cpp:211`).
- **Floor stepping**: UpFloor / DownFloor = PageUp / PageDown (`HK:79-80`; menu `menubar.cpp:453-457`).

---

## 2. Selection

### 2a. Modes

Selection mode is `qeglobals.d_select_mode` (`src/game/structs.hpp:1545-1557`, offset `0x71C04`):

`sel_brush` (0, default), `sel_vertex` (1), `sel_edge` (2), `sel_singlevertex` (3), `sel_curvepoint` (4), `sel_area` (5), `sel_terrainpoint` (6), `sel_terraintexture` (7), `sel_addpoint` (8), `sel_cycle_edge_direction_quad` (9), `sel_editpoint` (10).

- Face selection: **no face mode in this enum**. UNKNOWN whether faces are picked per-face by click in brush mode. Not evidenced in source.
- Drag Edges = E and Drag Vertices = V (`HK:66-67`; menu `menubar.cpp:1048-1052`).
- Mode 9 is used by the rope generator and toolbar toggle (`src/ggui/toolbar.cpp:799`, `toolbox.cpp:507`).

### 2b. Click-select, cycling, and select-through

- Click-select behaviour (stock binary): **UNKNOWN** from source.
- Cycle group selection = Tab, with Shift+Tab as the reverse (both are `Patch TAB` rows in the stock table, ids 33089; `HK:38-39`; menu `menubar.cpp:1294`).
- Select-prev / select-next = Shift+Comma / Shift+Period (`HK:77-78`); behaviour **UNKNOWN**.
- Alt-drag: "Alt always drags when multiple brushes are selected" (`preferences.cpp:210`). With it on, Alt+click-drag on multiple selected brushes moves instead of extruding.

### 2c. Select inside / touching / by class / by key

From the Selection > Select submenu (`src/ggui/menubar.cpp:1058-1095`):

| Menu entry | Hotkey | Notes |
|---|---|---|
| Select Targetname | none | menubar.cpp:1060 |
| Select Classname | none | menubar.cpp:1064 |
| Select By Key/Value | Shift+Ctrl+F (`HK:13`) | menubar.cpp:1068 |
| Select Connected | Alt+Ctrl+E (`HK:100`) | menubar.cpp:1072 |
| Select Complete Tall | none | menubar.cpp:1078 |
| Select Partial Tall | none | menubar.cpp:1082 |
| Select Touching | none | menubar.cpp:1086 |
| Select Inside | none | menubar.cpp:1090 |

- Select Targetted (stock Ctrl+E, `SelectTargettedEntities`) and Same Target (Ctrl+B) are in the stock table but have no menu entry in source.
- Select All Of Type = Shift+A and recursive = Alt+A (stock and `HK:169-170`). No menu entry in source. Behaviour of "of type" (classname vs brush type) is **UNKNOWN**.
- Select By Class: via the entity classname selector ("Select Classname", above) and the grid right-click context menu (`gui_grid_context_show_select`, `preferences.cpp:199`).
- Select Inside vs Touching semantics: UNKNOWN from source (menu labels only).

### 2d. Deselect, invert, delete, clone

- Deselect = Escape (`HK:72`). Invert = I (`HK:73`). Delete = Backspace (`HK:71`). Clone = Space (`HK:70`). Menu items at `menubar.cpp:1097-1105`.

### 2e. Hide / unhide

| Action | Hotkey | Source |
|---|---|---|
| Hide Selected | H | `HK:7`; `menubar.cpp:486` |
| Hide Unselected | Alt+H | `HK:8`; `menubar.cpp:490` |
| Show Hidden | Shift+H | `HK:9`; `menubar.cpp:498` |
| Show Last Hidden | Shift+Ctrl+H | `HK:10`; `menubar.cpp:494` |
| Hide by classname | Shift+Alt+Ctrl+H | `HK:186` |

### 2f. Filters

- Filter window: F (`HK:6`; `menubar.cpp:301`).
- Filter presets: `assets/bin/RadiantFilters.txt` (identical to the installed copy). It defines groups such as Structural, Detail, WeaponClip, NonColliding, Sky, Water, Translucent (`fdhide`), GoalVolumes (`fdhide`), Terrain/Curve (`fdshow`), and Face-level filters like `shadowcaster`, `foliage`, `traverse`. The meaning of the `fdshow`/`fdhide` flags is **UNKNOWN** (only the names appear in the file).
- Filter toggles `toggle_filter_all/ents/trigger/others/geo` exist in the ini but are all unbound (`HK:207-211`).

---

## 3. Grid and snapping

### 3a. Grid sizes

- Sizes: `GRID_SIZES = {0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512}` (`src/common/cxywnd.hpp:119-120`); enum at `cxywnd.hpp:122-134`.

| Grid | Hotkey (HK) | Menu / source |
|---|---|---|
| 0.5 | ~ (tilde) (`HK:56`) | `menubar.cpp:1331` |
| 1 | 1 (`HK:57`) | `menubar.cpp:1335` |
| 2 | 2 | `menubar.cpp:1339` |
| 4 | 3 | `menubar.cpp:1343` |
| 8 | 4 | `menubar.cpp:1347` |
| 16 | 5 | `menubar.cpp:1351` |
| 32 | 6 | `menubar.cpp:1355` |
| 64 | 7 | `menubar.cpp:1359` |
| 128 | **no hotkey** (menu passes `0`) | `menubar.cpp:1363` |
| 256 | 8 | `menubar.cpp:1367` |
| 512 | 9 | `menubar.cpp:1371` |

- `0` = ToggleGrid (`HK:55`); visibility behaviour in the binary (UNKNOWN in detail).
- `[` = GridDown and `]` = GridUp (`HK:138-139`). Stepping semantics (one size step, or power-of-two) are **UNKNOWN**; the names suggest one step through the size list.
- Setting a grid size writes the grid size to saved settings when `m_bSnapTToGrid` is on (`menubar.cpp:5-16`). The UI that sets `m_bSnapTToGrid` was not found in source (UNKNOWN).

### 3b. Snap on/off

- Menu "Snap To Grid" is checked when `!prefs->m_bNoClamp` (`menubar.cpp:1375`); click calls `CMainFrame::OnSnaptogrid` at `0x428380`.
- Preference: "Disable grid snapping" (`preferences.cpp:205`, tooltip "Org: Don't clamp plane points"). Its scope is described only as plane points; snapping of move, resize and vertex edits is in the binary and **UNKNOWN** from source.
- Toggle hotkey: Alt+Ctrl+G (`ToggleSnapToGrid`, stock and `HK:166`).
- Ctrl+G = `SelectSnapPointsToGrid` (stock and `HK:167`). The behaviour (snap selected points to grid) is **UNKNOWN** beyond the name; no menu entry in source.
- Snapping is temporarily disabled while computing a prefab midpoint (`src/ggui/camera.cpp:626-627`, restored at `:743` and `:751`) so that stamped prefabs land on the expected origin.

### 3c. Guizmo snapping

- `guizmo_snapping` dvar, default **true** (`src/common/camwnd.cpp:389-393`); menu "Snap To Grid - Guizmo" (`menubar.cpp:1379`). Guizmo enable: `guizmo_enable`, default **true** (`camwnd.cpp:383-387`).
- Guizmo world/local: Minus (`HK:216`; `menubar.cpp:1180`).

### 3d. Vertex welding (not grid snap, but related)

- Weld to model, brush or prefab vertices: `m_bVertSnapModel/Brush/Prefab` (`menubar.cpp:1878-1886`). Tolerant weld distance: default UNKNOWN, range 1 to 128 (`preferences.cpp:179`). Tolerant weld hotkey: Shift+Ctrl+J (stock and `HK:27`).

### 3e. Grid window options

- `grid_zoom_to_cursor` default true; `grid_draw_edge_coordinates` default true (`cxywnd.cpp:717-727`); `grid_new_patch_drag` default false (`cxywnd.cpp:729-733`, toggled by `new_patch_drag`, Shift+Q, `HK:217`).

---

## 4. Hotkeys

How the table was built:

- "Stock default" = the 187-entry `g_Commands` table from `IW3xRadiant.exe` (VA `0x73B240`, read statically). Key names decoded with the table in `src/ggui/hotkeys.cpp:196-242`.
- "iw3xo hotkeys.ini" = the shipped `assets/bin/IW3xRadiant/hotkeys.ini`, which overrides stock at load. `(unbound)` means the key is blank in the ini.
- Commands that exist only in the ini (addons registered with `register_command_with_hotkey` or `cmd_addon_hotkeys`, `src/components/modules/command.cpp:15-19`) show `n/a` for stock.
- `Patch TAB` appears twice in the ini (Tab and Shift+Tab). `Vertex Select Up/Down` are in the stock table as Ctrl+Up / Ctrl+Down.
- Conflicts in the shipped ini: `TexRotateClock` and `TexScaleLeft` both use Ctrl+Left; `TexRotateCounter` and `TexScaleRight` both use Ctrl+Right. The editor flags these in the Hotkeys window (`hotkeys.cpp`, `do_row` / `cmdbinds_check_dupe`).
- Notable ini changes from stock: `CenterView` End to Ctrl+End; `MapInfo` M to Ctrl+M; `PatchInspector`, `ToggleCamera`, `ToggleZ`, `SameTargetname`, `LightPreviewSun`, `RefreshTextures` (F5), `ToggleLayeredMaterialWnd` (F4), `ToggleTurnTerrainEdges` unbound. `center_camera_on_selection` takes End.
- Hotkey editor: Edit > Edit Hotkeys (`menubar.cpp:278`). Changes apply on close and write `IW3xRadiant/hotkeys.ini` under `fs_homepath` (`hotkeys.cpp:on_close`). If no ini exists, the editor generates one from the stock table on load (`hotkeys.cpp:382-411`).

Modifier shorthand in the table: `Shift+`, `Alt+`, `Ctrl+`, applied in that order.

| Command | Stock default (binary) | iw3xo hotkeys.ini | Notes |
|---|---|---|---|
| ToggleOutlineDraw | J | J | Renderer > Outline Selected (menubar.cpp:509) |
| ToggleTintDraw | Shift+J | Shift+J | Renderer > Tint Selected (menubar.cpp:513) |
| CSGMerge | Ctrl+U | Ctrl+U | CSG > Merge (menubar.cpp:1197) |
| AutoCaulk | Alt+C | Alt+C | CSG > Auto Caulk (menubar.cpp:1201) |
| ViewFilters | F | F | Filter window (menubar.cpp:301) |
| HideSelected | H | H | View > Hide Selected (menubar.cpp:486) |
| HideUnSelected | Alt+H | Alt+H | View > Hide Unselected (menubar.cpp:490) |
| ShowHidden | Shift+H | Shift+H | View > Show Hidden (menubar.cpp:498) |
| ShowLastHidden | Shift+Ctrl+H | Shift+Ctrl+H | View > Show Last Hidden (menubar.cpp:494) |
| FitBrush | Shift+B | Shift+B |  |
| AdvancedCurveEdit | Y | Y |  |
| SelectionKeyValue | Shift+Ctrl+F | Shift+Ctrl+F | Select > Select By Key/Value (menubar.cpp:1068) |
| ToggleLockPatchVertices | Ctrl+Period | Ctrl+Period |  |
| ToggleUnlockPatchVertices | Shift+Ctrl+Period | Shift+Ctrl+Period |  |
| ToggleTurnTerrainEdges | Ctrl+/ | (unbound) | Stock Ctrl+/; ini leaves unbound |
| ShowTexturesInUse | U | U |  |
| ViewTextures | T | T | Textures (menubar.cpp:341) |
| ThickenPatch | Shift+Ctrl+T | Shift+Ctrl+T |  |
| AddTerrainRow | Shift+Ctrl+A | Shift+Ctrl+A |  |
| ExtrudeTerrainRow | Alt+O | Alt+O |  |
| RemoveTerrainRow | Shift+Ctrl+Q | Shift+Ctrl+Q |  |
| SplitPatch | Shift+Ctrl+X | Shift+Ctrl+X |  |
| SurfaceInspector | S | S | Surface Inspector (menubar.cpp:317) |
| PatchInspector | Shift+S | (unbound) | Stock Shift+S; ini unbound |
| ApplyPatchCap | Shift+Ctrl+P | Shift+Ctrl+P |  |
| TolerantWeld | Shift+Ctrl+J | Shift+Ctrl+J |  |
| RedisperseVertices | Shift+F | Shift+F |  |
| RedisperseRows | Shift+E | Shift+E |  |
| RedisperseCols | Shift+Ctrl+E | Shift+Ctrl+E |  |
| InvertCurveTextureX | Shift+Ctrl+I | Shift+Ctrl+I |  |
| InvertCurveTextureY | Shift+I | Shift+I |  |
| InvertCurve | Ctrl+I | Ctrl+I |  |
| IncPatchColumn | Shift+Ctrl+Plus | Shift+Ctrl+Plus |  |
| IncPatchRow | Ctrl+Plus | Ctrl+Plus |  |
| DecPatchColumn | Shift+Ctrl+Subtract | Shift+Ctrl+Subtract |  |
| DecPatchRow | Ctrl+Subtract | Ctrl+Subtract |  |
| Patch TAB | Tab / Shift+Tab (two stock rows) | Tab / Shift+Tab (two ini rows) | Cycle group selection; Shift+Tab is the 2nd ini row (duplicate name). Menu: Selection > Cycle Group Selection (menubar.cpp:1294) |
| TogglePatchWireframes | Shift+W | Shift+W |  |
| SelectNudgeDown | Alt+Down | Alt+Down |  |
| EntityColor | K | K |  |
| CameraForward | Up | Up | Arrow keys move camera (Forward/Back/Left/Right) |
| CameraBack | Down | Down |  |
| CameraLeft | Left | Left |  |
| CameraRight | Right | Right |  |
| Vertex Select Up | Ctrl+Up | Ctrl+Up | Stock table entry (binary); ini unchanged |
| Vertex Select Down | Ctrl+Down | Ctrl+Down | Stock table entry (binary); ini unchanged |
| CameraUp | D | D |  |
| CameraDown | C | C |  |
| CameraAngleUp | A | A | A / Z pitch |
| CameraAngleDown | Z | Z |  |
| CameraStrafeRight | Period | Period |  |
| CameraStrafeLeft | Comma | Comma | Comma / Period strafe |
| ToggleGrid | 0 | 0 |  |
| SetGridPointFive | ~ | ~ | Sets 0.5 grid (menubar.cpp:1331) |
| SetGrid1 | 1 | 1 |  |
| SetGrid2 | 2 | 2 |  |
| SetGrid4 | 3 | 3 |  |
| SetGrid8 | 4 | 4 |  |
| SetGrid16 | 5 | 5 |  |
| SetGrid32 | 6 | 6 |  |
| SetGrid64 | 7 | 7 |  |
| SetGrid256 | 8 | 8 |  |
| SetGrid512 | 9 | 9 |  |
| DragEdges | E | E | Selection > Drag Edges (menubar.cpp:1048) |
| DragVertices | V | V | Selection > Drag Vertices (menubar.cpp:1052) |
| ViewEntityInfo | N | N | Entity window (menubar.cpp:305) |
| ViewConsole | O | O | Console (menubar.cpp:297) |
| CloneSelection | Space | Space | Selection > Clone (menubar.cpp:1101) |
| DeleteSelection | Backspace | Backspace | Edit > Delete (menubar.cpp:243) |
| UnSelectSelection | Escape | Escape | Selection > Deselect (menubar.cpp:1097) |
| InvertSelection | I | I | Selection > Invert (menubar.cpp:1105) |
| CenterView | End | Ctrl+End | Stock End; ini moves it to Ctrl+End (End goes to center_camera_on_selection) |
| ZoomOut | Insert | Insert | Insert; Ctrl+Insert is ZZoomOut |
| ZoomIn | Delete | Delete | Delete; Ctrl+Delete is ZZoomIn |
| SelectPrev | Shift+Comma | Shift+Comma |  |
| SelectNext | Shift+Period | Shift+Period |  |
| UpFloor | PageUp | PageUp | View > Up Floor (menubar.cpp:453) |
| DownFloor | PageDown | PageDown | View > Down Floor (menubar.cpp:457) |
| LinkSelectionToggle | Shift+O | Shift+O |  |
| ToggleClipper | X | X | Clipper toggle (menubar.cpp:1210) |
| ToggleCrosshairs | Shift+X | Shift+X | Renderer > Draw Crosshair (menubar.cpp:546) |
| ToggleTexMoveLock | Shift+T | Shift+T | Texture Lock > Moves (menubar.cpp:1439) |
| ToggleTexRotateLock | Shift+R | Shift+R | Texture Lock > Rotations (menubar.cpp:1443) |
| ToggleLightmapLock | (unbound) | (unbound) | Unbound in both; menu Texture Lock > Lightmaps (menubar.cpp:1447) |
| RemoveColorNode | Ctrl+R | Ctrl+R |  |
| ToggleLayers | L | L | Layers (menubar.cpp:337) |
| Preferences | P | P | Edit > Preferences (menubar.cpp:282) |
| ToggleCamera | Shift+Ctrl+C | (unbound) | Stock Shift+Ctrl+C; ini unbound |
| ToggleView | Shift+Ctrl+V | Shift+Ctrl+V |  |
| DropVertices | Shift+Alt+Ctrl+D | Shift+Alt+Ctrl+D |  |
| RotateZ | Shift+D | Shift+D | Rotate Z (menubar.cpp:1138) |
| ToggleZ | Shift+Ctrl+Z | (unbound) | Stock Shift+Ctrl+Z; ini unbound |
| SameTargetname | B | (unbound) | Stock B; ini unbound |
| SameTarget | Ctrl+B | Ctrl+B |  |
| ConnectSelection | W | W | Selection > Connect Entities (menubar.cpp:1282) |
| SetViewToEntity | F6 | F6 |  |
| SplaySelection | Alt+W | Alt+W | Stock Alt+W; Splay distance pref (preferences.cpp:180) |
| SelectConnectedEntities | Alt+Ctrl+E | Alt+Ctrl+E | Select > Select Connected (menubar.cpp:1072) |
| SelectTargettedEntities | Ctrl+E | Ctrl+E | Stock Ctrl+E; no menu entry found |
| PatchMatrixTranspose | Shift+Ctrl+M | Shift+Ctrl+M |  |
| MakeDetail | Shift+Ctrl+D | Shift+Ctrl+D | Selection > Make Detail (menubar.cpp:1300) |
| MakeWeaponClip | Shift+Ctrl+W | Shift+Ctrl+W | Selection > Make Weapon Clip (menubar.cpp:1308) |
| MakeNonColliding | Shift+Ctrl+Minus | Shift+Ctrl+Minus | Selection > Make Non-Colliding (menubar.cpp:1312) |
| MakeSplitCoplanarGeo | (unbound) | (unbound) |  |
| MakeDontSplitCoplanarGeo | (unbound) | (unbound) |  |
| MapInfo | M | Ctrl+M | Stock M; ini moves it to Ctrl+M |
| NextLeakSpot | Shift+Ctrl+K | Shift+Ctrl+K |  |
| PrevLeakSpot | Shift+Ctrl+L | Shift+Ctrl+L |  |
| FileOpen | Ctrl+O | Ctrl+O | File > Open (menubar.cpp:72) |
| FileSave | Ctrl+S | Ctrl+S | File > Save (menubar.cpp:116) |
| Quit | (unbound) | (unbound) | Stock unbound; File > Exit menu shows it (menubar.cpp:213) |
| NextView | Ctrl+Tab | Ctrl+Tab | View > Next (menubar.cpp:463) |
| ClipSelected | Return | Return | Clipper > Clip Selected (menubar.cpp:1214) |
| SplitSelected | Shift+Return | Shift+Return | Clipper > Split Selected (menubar.cpp:1218) |
| FlipClip | Ctrl+Return | Ctrl+Return | Clipper > Flip Clip Orientation (menubar.cpp:1222) |
| MouseRotate | R | R | Free rotation toggle (menubar.cpp:1142) |
| Copy | Ctrl+C | Ctrl+C | Edit > Copy (menubar.cpp:235) |
| Paste | Ctrl+V | Ctrl+V | Edit > Paste (menubar.cpp:239) |
| Undo | Ctrl+Z | Ctrl+Z | Greys out when no undo entry (menubar.cpp:225) |
| Redo | Ctrl+Y | Ctrl+Y | Greys out when redo stack empty (menubar.cpp:229) |
| ZZoomOut | Ctrl+Insert | Ctrl+Insert |  |
| ZZoomIn | Ctrl+Delete | Ctrl+Delete |  |
| TexDecrement | Shift+Subtract | Shift+Subtract |  |
| TexIncrement | Shift+Plus | Shift+Plus |  |
| TextureFit | Ctrl+F | Ctrl+F |  |
| TextureFitAll | Alt+Ctrl+F | Alt+Ctrl+F |  |
| TexRotateClock | Ctrl+Left | Ctrl+Left |  |
| TexRotateCounter | Ctrl+Right | Ctrl+Right |  |
| TexShiftLeft | Shift+Left | Shift+Left |  |
| TexShiftRight | Shift+Right | Shift+Right |  |
| TexShiftUp | Shift+Up | Shift+Up |  |
| TexShiftDown | Shift+Down | Shift+Down |  |
| TexLayerCycle | Shift+L | Shift+L |  |
| TexLayerMaterial | (unbound) | (unbound) |  |
| TexLayerLightmap | (unbound) | (unbound) |  |
| GridDown | [ | [ |  |
| GridUp | ] | ] |  |
| TexScaleLeft | Ctrl+Left | Ctrl+Left |  |
| TexScaleRight | Ctrl+Right | Ctrl+Right |  |
| LightShiftUp | Alt+] | Alt+] |  |
| LightShiftDown | Alt+[ | Alt+[ |  |
| CyclinderHeightUp | Alt+Period | Alt+Period |  |
| CyclinderHeightDown | Alt+Comma | Alt+Comma |  |
| AssociateEntities | Shift+G | Shift+G |  |
| VehicleGroup | Shift+V | Shift+V |  |
| DynEntities | Shift+Y | Shift+Y |  |
| DisassociateEntities | Shift+Ctrl+G | Shift+Ctrl+G |  |
| SelectedAssociated | Ctrl+X | Ctrl+X |  |
| OverBrightShiftUp | Shift+] | Shift+] |  |
| OverBrightShiftDown | Shift+[ | Shift+[ |  |
| CubicClipZoomOut | Ctrl+] | Ctrl+] |  |
| CubicClipZoomIn | Ctrl+[ | Ctrl+[ |  |
| ToggleCubicClip | Ctrl+\ | Ctrl+\ | Cubic Clipping (menubar.cpp:923) |
| MoveSelectionDOWN | Subtract | Subtract |  |
| MoveSelectionUP | Plus | Plus |  |
| LinkSelected | Alt+Q | Alt+Q | Selection > Link Entities (menubar.cpp:1286) |
| SelectNudgeLeft | Alt+Left | Alt+Left |  |
| GetDistance | Alt+F1 | Alt+F1 |  |
| AutoEdgeTurn | Alt+F2 | Alt+F2 |  |
| SelectNudgeRight | Alt+Right | Alt+Right |  |
| SelectNudgeUp | Alt+Up | Alt+Up |  |
| CycleCapTexturePatch | Shift+Ctrl+N | Shift+Ctrl+N |  |
| NaturalizePatch | Ctrl+N | Ctrl+N |  |
| ToggleSnapToGrid | Alt+Ctrl+G | Alt+Ctrl+G | Grid > Snap To Grid (menubar.cpp:1375) |
| SelectSnapPointsToGrid | Ctrl+G | Ctrl+G | Stock Ctrl+G; no menu entry found |
| ShowAllTextures | Ctrl+A | Ctrl+A | Stock Ctrl+A; menu item commented out (menubar.cpp:1394) |
| SelectAllOfType | Shift+A | Shift+A |  |
| SelectAllOfTypeRecurse | Alt+A | Alt+A |  |
| CapCurrentCurve | Shift+C | Shift+C |  |
| MakeStructural | Shift+Ctrl+S | Shift+Ctrl+S | Selection > Make Structural (menubar.cpp:1304) |
| Center2DOnCamera | Alt+X | Alt+X | View > Center 2D On Camera (menubar.cpp:445) |
| EnterPrefab | Alt+PageDown | Alt+PageDown | Edit > Enter Prefab (menubar.cpp:260); entering/leaving clears undo |
| LeavePrefab | Alt+PageUp | Alt+PageUp | Edit > Leave Prefab (menubar.cpp:264) |
| LightPreviewToggle | F8 | F8 |  |
| LightPreviewStart | Shift+F8 | Shift+F8 |  |
| LightPreviewStop | Shift+Alt+F8 | Shift+Alt+F8 |  |
| LightPreviewClear | Alt+F8 | Alt+F8 |  |
| LightPreviewSun | Ctrl+F8 | (unbound) | Stock Ctrl+F8; ini unbound |
| LightPreviewRegions | Shift+Ctrl+F8 | Shift+Ctrl+F8 |  |
| MaxLightIntensity | Alt+Ctrl+F8 | Alt+Ctrl+F8 |  |
| VertEdit | G | G | Stock G; Vert Edit dialog (menubar.cpp:419) |
| RefreshTextures | F5 | (unbound) | Stock F5; ini unbound (F5 used by bsp_compile) |
| TogglePreviewModels | (unbound) | (unbound) |  |
| HideByClassname | Shift+Alt+Ctrl+H | Shift+Alt+Ctrl+H |  |
| ToggleLayeredMaterialWnd | F4 | (unbound) | Stock F4; ini unbound (F4 used by toggle_bsp_radiant) |
| SaveLayeredMaterials | (unbound) | (unbound) |  |
| LockX | n/a (not in stock table) | (unbound) |  |
| LockY | n/a (not in stock table) | (unbound) |  |
| LockZ | n/a (not in stock table) | (unbound) |  |
| xo_gameview | n/a (not in stock table) | F9 | View > Toggle > Game View (menubar.cpp:293) |
| xo_modelselector | n/a (not in stock table) | M |  |
| fakesun_toggle | n/a (not in stock table) | Ctrl+F8 | Renderer > Fake Sun Preview (menubar.cpp:580) |
| fakesun_fog_toggle | n/a (not in stock table) | (unbound) |  |
| filmtweak_toggle | n/a (not in stock table) | Ctrl+F9 |  |
| fx_play | n/a (not in stock table) | (unbound) |  |
| fx_repeat | n/a (not in stock table) | (unbound) |  |
| fx_pause | n/a (not in stock table) | (unbound) |  |
| fx_stop | n/a (not in stock table) | (unbound) |  |
| fx_browser | n/a (not in stock table) | (unbound) |  |
| center_camera_on_selection | n/a (not in stock table) | End | Added by addon (camwnd.cpp:458) |
| bsp_reload | n/a (not in stock table) | (unbound) |  |
| bsp_compile | n/a (not in stock table) | F5 | d3dbsp > Compile (menubar.cpp:1027) |
| toggle_bsp | n/a (not in stock table) | (unbound) |  |
| toggle_bsp_radiant | n/a (not in stock table) | F4 | d3dbsp toggle (menubar.cpp:1009) |
| toggle_filter_ents | n/a (not in stock table) | (unbound) |  |
| toggle_filter_trigger | n/a (not in stock table) | (unbound) |  |
| toggle_filter_others | n/a (not in stock table) | (unbound) |  |
| toggle_filter_all | n/a (not in stock table) | (unbound) |  |
| toggle_filter_geo | n/a (not in stock table) | (unbound) |  |
| toggle_toolbox | n/a (not in stock table) | Q | View > Toggle > Toolbox (menubar.cpp:353) |
| brush_from_selected | n/a (not in stock table) | (unbound) |  |
| prefab_browser | n/a (not in stock table) | B |  |
| physx_movement | n/a (not in stock table) | F3 | Toggles PhysX movement; double-jump to fly (camera.cpp:219) |
| guizmo_world_local | n/a (not in stock table) | Minus | Toggles guizmo world/local (menubar.cpp:1180) |
| new_patch_drag | n/a (not in stock table) | Shift+Q | Toggles grid new-patch dragging (menubar.cpp:1385) |

## 5. Undo and redo

### 5a. Keys and menu

- Undo = Ctrl+Z, Redo = Ctrl+Y (stock and `HK:121-122`). Edit menu greys Undo when there is no undoable entry (`src/ggui/menubar.cpp:225-232`).

### 5b. Depth and memory

- Default depth: registry `UndoLevels`, default **512** (`src/common/radiantapp.cpp:125`). The code comment says the engine default is 64 otherwise (`radiantapp.cpp:124`).
- Slider: "Undo levels", clamped 16 to 512 (`src/ggui/preferences.cpp:165-168`), writes `g_undoMaxSize` (`0x739F6C`, `functions.cpp:232`).
- Memory: `main_module.cpp:462-463` sets the value at `0x739F70` to `0x01000000` with the comment "default 2mb, now 16mb". The field's exact meaning (hard byte cap or budget) is **UNKNOWN**; the comment is the only evidence.

### 5c. What creates undo entries

Undo entries are opened with `Undo_GeneralStart(op)`, filled with `Undo_AddBrushList` / `Undo_EndBrushList`, and closed with `Undo_End`. A new operation calls `Undo_ClearRedo` first (`src/game/functions.cpp:236-280`). Call sites in source:

- Patch capping, `Patch_CapCurrent` (`src/components/modules/pmesh.cpp:5-14`, `:113-114`).
- Patch brush-list hooks (`src/components/modules/patches.cpp:7-43`).
- Create brush from selection (`src/components/modules/main_module.cpp:580-619`).
- Mesh painter start and end (`src/components/modules/mesh_painter.cpp:407-641`).
- Create entity from physics (`src/components/modules/physx_impl.cpp:1212-1302`).
- Delete old prefab during stamping (`src/ggui/camera.cpp:700-716`).

Entities created with `CreateEntityFromName_DisableUndo` skip the brush-list hooks (`src/game/functions.hpp:321-322`, `src/components/modules/mesh_painter.cpp:555-559`). Property edits that should not be undoable use an `addprop_helper_s` with `no_undo` (`src/ggui/entity.cpp:326`, `camera.cpp:1228-1383`). Entity property drags do go through `drag_float_helper_undo` (`entity.cpp:326-329`).

Undo and redo are logged to the console: "[UNDO] '<op>' added to the undo stack" and "[UNDO] '<op>' undone" (`src/components/modules/main_module.cpp:395-432`).

### 5d. Known issues and gaps (from source comments)

- Entering or leaving a prefab clears the entire undo stack (`src/ggui/camera.cpp:694`, comment "!! entering / leaving a prefab clears the entire undo stack !!").
- Mesh painter: `Undo_AddBrushList_Selected` / `Undo_EndBrushList_Selected` are commented out (`mesh_painter.cpp:409`, `:640`), so painter undo may not restore brush contents. Treat as unverified.
- PhysX entity creation has a comment "works #1" on `Undo_End` (`physx_impl.cpp:1302`), suggesting it was fixed by trial.
- Whether camera and free-fly moves are undoable: **UNKNOWN** (no undo calls in the camera code path).
- Whether undo survives a map save and reload: **UNKNOWN**.

---

## 6. Other comfort features

| Feature | Hotkey(s) | Notes and source |
|---|---|---|
| Texture lock: moves | Shift+T | `HK:84`; `menubar.cpp:1439`; pref `m_bTextureLock` (`afx.hpp:1319`) |
| Texture lock: rotations | Shift+R | `HK:85`; `menubar.cpp:1443`; `m_bRotateLock` (`afx.hpp:1377`) |
| Texture lock: lightmaps | unbound | `menubar.cpp:1447`; `m_bLightmapLock` (`afx.hpp:1321`) |
| Texture fit / fit all | Ctrl+F / Alt+Ctrl+F | stock and `HK:127-128` |
| Texture shift, rotate, scale | Shift+Arrows, Ctrl+Left/Right | `HK:129-134`, `HK:140-141` (note the Ctrl+Left/Right conflict above) |
| Clipper toggle | X | `HK:82`; `menubar.cpp:1210` |
| Clip selected | Return | `HK:115`; `menubar.cpp:1214` |
| Split selected | Shift+Return | `HK:116`; `menubar.cpp:1218` |
| Flip clip orientation | Ctrl+Return | `HK:117`; `menubar.cpp:1222` |
| CSG merge | Ctrl+U | `HK:4`; `menubar.cpp:1197` |
| CSG auto caulk | Alt+C | `HK:5`; `menubar.cpp:1201` |
| CSG hollow | no hotkey | `menubar.cpp:1193` |
| Cubic clipping toggle | Ctrl+\ | `HK:155`; `menubar.cpp:923`; scale `m_nCubicScale` (`menubar.cpp:921`); zoom Ctrl+[ / Ctrl+] (`HK:153-154`) |
| Surface inspector | S | `HK:24`; `menubar.cpp:317`, `1402` |
| Entity window | N | `HK:68`; `menubar.cpp:305` |
| Console | O | `HK:69`; `menubar.cpp:297` |
| Textures | T | `HK:18`; `menubar.cpp:341` |
| Layers | L | `HK:88`; `menubar.cpp:337` |
| Model browser | M | `HK:193` (addon, `xo_modelselector`) |
| Prefab browser | B | `HK:214` (addon, `prefab_browser`) |
| Toolbox | Q | `HK:212` (`toggle_toolbox`) |
| Preferences | P | `HK:89`; `menubar.cpp:282` |
| Free rotation | R | `HK:118`; `menubar.cpp:1142` |
| Rotate Z | Shift+D | `HK:93`; `menubar.cpp:1138` |
| Guizmo world/local | Minus | `HK:216`; `menubar.cpp:1180`; guizmo on by default (`camwnd.cpp:383`) |
| Make detail / structural / weapon clip / non-colliding | Shift+Ctrl+D / S / W / Minus | `HK:103-105`, `HK:172`; `menubar.cpp:1300-1312` |
| Enter / leave prefab | Alt+PageDown / Alt+PageUp | `HK:174-175`; `menubar.cpp:260-264` |
| Lights preview | F8 family | `HK:176-182`; `menubar.cpp:856-904` |
| Game view | F9 | `HK:192` (addon, `xo_gameview`) |
| Compile d3dbsp | F5 | `HK:204` (addon, `bsp_compile`) |
| Crosshair | Shift+X | `HK:83`; `menubar.cpp:546` |
| Outline / tint selected | J / Shift+J | `HK:1-2`; `menubar.cpp:509-513` |
| Right-click context menus | RMB in grid/camera | new grid context menu toggle (`preferences.cpp:194`); `gui_grid_context_show_select` (`preferences.cpp:199`) |
| Snapshots, autosave, lose-changes prompt | prefs | `preferences.cpp:140-148` |
| Zoom to cursor | pref | `preferences.cpp:204`, `grid_zoom_to_cursor` |
| Mouse-origin display, sizing info | prefs | `preferences.cpp:216-217` |
| Alt-drag multi-brush | pref | `preferences.cpp:210` |

Not found in source, so UNKNOWN for this list: a stock "Tie to Entity" or group-move command, texture-lock behaviour under rotation, and an equivalent of stock "select all of type" in the menu.

---

## Source list

Repository (`E:\IW8-Radiant\reference\iw3xo-radiant\`):

- `assets/bin/IW3xRadiant/hotkeys.ini`, `assets/bin/RadiantFilters.txt`
- `src/common/camwnd.cpp`, `src/common/camwnd.hpp`, `src/common/cxywnd.cpp`, `src/common/cxywnd.hpp`, `src/common/mainframe.cpp`, `src/common/radiantapp.cpp`, `src/common/afx.hpp`
- `src/ggui/hotkeys.cpp`, `src/ggui/menubar.cpp`, `src/ggui/preferences.cpp`, `src/ggui/camera.cpp`, `src/ggui/modelselector.cpp`, `src/ggui/toolbar.cpp`, `src/ggui/entity.cpp`
- `src/game/functions.cpp`, `src/game/functions.hpp`, `src/game/structs.hpp`
- `src/components/modules/main_module.cpp`, `pmesh.cpp`, `patches.cpp`, `mesh_painter.cpp`, `physx_impl.cpp`, `command.cpp`
- `src/common/physx/cct_camera.cpp`

Installed (read-only):

- `E:\CoD4\game_build\Call of Duty 4 Modern Warfare\bin\IW3xRadiant.exe` (stock table at VA `0x73B240`, 187 entries, parsed statically)
- `E:\CoD4\radiant\bin\IW3xRadiant.exe` (same table)
- `...\bin\IW3xRadiant\hotkeys.ini`, `...\bin\IW3xRadiant\dvars.cfg` (installed copies), `...\bin\RadiantFilters.txt`

External:

- https://wiki.zeroy.com/index.php/Call_of_Duty_4:_Radiant (camera key examples, Preferences = P)

Not retrieved: the Radiant Command List referenced from atwiki, and the iw3xo wiki (search did not return the hotkey page).
