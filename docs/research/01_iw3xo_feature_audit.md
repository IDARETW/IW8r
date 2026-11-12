# iw3xo-radiant feature audit (mapper view)

Scope: pinned clone `E:\IW8-Radiant\reference\iw3xo-radiant`, commit `7c3a409921cdb453c204506e4df8448607999b94` (develop). All paths below are relative to that repo root unless absolute. Read-only audit; no build or run.

Origin key: **added** = new iw3xo implementation with no stock equivalent; **enhanced** = iw3xo hooks or replaces a stock CoD4 Radiant function and changes its behaviour; **inherited** = stock CoD4 Radiant behaviour. A menu entry that is only an ImGui re-skin calling a stock `CMainFrame` handler by address is labelled `inherited (ImGui re-skin)`.

Evidence caveat: `gh` is not installed on this machine, so release data came from WebFetch of the GitHub releases pages and the GitHub wiki Changelog. Those are secondhand summaries from a small model, not raw release-note text. Release-based claims are marked `[rel]`.

---

## 1. Build and architecture

iw3xo is a 32-bit (Win32, `architecture "x86"`, `premake5.lua:227-228`) SharedLib, `iw3r.dll`, that is loaded into the stock **CoD4Radiant.exe** process. `src/main.cpp:19-42` patches the stock PE entry point at `0x5C4299` (the comment at `main.cpp:37` says the image size is `0x234D000`), jumps into `main::initialize()`, then calls `components::loader::initialize()` (`src/components/loader.cpp:8-46`), which registers about 22 modules (`src/components/modules/*`). The launcher `assets/bin/IW3xRadiant.exe` is a prebuilt binary with no source here, and the README install steps say to replace `iw3r.dll` in `bin/` (`README.md:118-125`). Hooking uses the in-repo `utils::hook` API (`src/utils/hooking.cpp`), which can jump, call, nop or detour at fixed stock addresses; `src/game/functions.hpp` and `src/game/functions.cpp` hold about 133 hard-coded stock addresses and their wrapper types (for example `functions.hpp:95-98` for stock data pointers). The renderer is Direct3D 9, with iw3xo wrapping the device in a proxy class (`src/components/modules/d3d9ex.hpp`, `d3d9ex.cpp:273`, adapted from the IW4x client as its header comment says) so that `EndScene` can drive the UI (`src/components/modules/gui.cpp:272-290`). The UI is Dear ImGui (docking branch, `.gitmodules` points at `xoxor4d/imgui` `docking-update`) with ImGuizmo (`.gitmodules`, used by `src/ggui/camera_guizmo.cpp`), imgui_notify toasts (`src/common/imgui/imgui_notify.hpp`), a custom L2D file dialog (`src/ggui/l2d_file_dialog.cpp`), Font Awesome 5 (`src/common/imgui/font_awesome_5.h`) and a curve editor (`src/common/imgui/imgui_curves.cpp`). The physics stack is NVIDIA PhysX (version not verified; `deps/physx`, copied next to the DLL by `premake5.lua:` post-build step; PhysX code in `src/common/physx/` and `src/components/modules/physx_impl.cpp`). Other deps: discord-rpc (`src/components/modules/discord.cpp`), rapidjson, WinHttpClient (update check in `main_module.cpp:5-12`), detours (`deps/detours`), DirectX SDK headers (`deps/dxsdk`), fifo_map and the Lumix-derived ImGui curve code. The external compilers are not part of the repo: `bin/cod4map`, `bin/cod4rad` and `linker_pc.exe` are stock CoD4 tools called through batch wrappers (see section 2, compilation).

---

## 2. Feature table

Row count and order follow the categories requested. "Sees" = the one-line mapper-visible effect.

| Feature | Category | Origin | Evidence | What the mapper sees |
|---|---|---|---|---|
| Docked, tabbed ImGui UI with saved layouts | UI/layout | added | `src/components/modules/gui.cpp:79` (`handle_dockspace`), `README.md:69` | Dockable panels that replace the MFC window arrangement. |
| ImGui menubar replacing MFC menu | UI/layout | added (replaces stock menu, stock items still reachable) | `src/ggui/menubar.cpp:60` (`BeginMenuBar`), `menubar.cpp:363-420` "Original Windows" submenu, `src/common/mainframe.cpp:865` dvar `mainframe_show_menubar` | Full menubar, plus a submenu to bring back the original stock windows. |
| Editable toolbars | UI/layout | added | `src/ggui/toolbar.cpp:1347`, `toolbar.cpp:1501` (Toolbar Editor), `README.md:94` | Toolbar buttons can be added, removed and rearranged. |
| Editable colors | UI/layout | added | `src/ggui/colors.cpp:17`, `README.md:94` | Editor colors (for example Clipper at `colors.cpp:63`) are set in a window. |
| Preferences window | UI/layout | enhanced (ImGui re-skin of stock prefs) | `src/ggui/preferences.cpp:631`, `menubar.cpp:282` | Preferences in an ImGui window. |
| Toast notifications | UI/layout | added | `src/common/imgui/imgui_addons.cpp:34` (`Toast`), `src/components/modules/gui.cpp:479`, `gui.cpp:620` (autosave toast), `README.md:93` | Short pop-up messages for saves, autosaves and events. |
| New file dialogs with working default paths | UI/layout | added | `src/ggui/l2d_file_dialog.cpp:44`, dvar `gui_use_new_filedialog` `src/components/modules/gui.cpp:823`, `[rel] 350`, `[rel] 352` (search) | ImGui file browser with search, instead of the stock Windows dialog. |
| Console with dvar suggestions and autocomplete | UI/layout | added | `src/ggui/console.cpp:601`, `console.cpp:748` (autocomplete popup), `src/components/modules/command.cpp`, `config.cpp`, `README.md:101` | Console window with dvar name completion. |
| Help window and About | UI/layout | added | `src/ggui/help.cpp:14`, `help.cpp:23` (links to tutorials), `src/ggui/about.cpp:19` | In-app help links and version window. |
| Hotkey editor and hotkey helper | hotkeys | added | `src/ggui/hotkeys.cpp:16` (helper), `hotkeys.cpp:652` (editor), `assets/bin/IW3xRadiant/hotkeys.ini` (defaults) | Every menu item shows its hotkey; the table can be edited in a window. |
| Hotkey lookup in menu items | hotkeys | added | `src/ggui/menubar.cpp` (many `hotkey_dialog::get_hotkey_for_command`, e.g. line 72) | Menu labels display the current (possibly user-remapped) keys. |
| Update check on startup | other | added | `src/components/modules/main_module.cpp:40` (`update_check`), `main_module.cpp:300` (thread start), `[rel] 350` | Startup check for a newer iw3xo release. |
| Discord rich presence | other | added | `src/components/modules/discord.cpp:30-77`, `src/common/radiantapp.cpp:108,131` (profile key `discord_rpc`), `[rel] 330` | Optional Discord status showing the map or prefab being edited. |
| Print parsed entity / brush counts on map load | other | added (off by default) | `src/components/modules/main_module.cpp:356-380`, dvar `parse_debug` `src/game/dvars.cpp:308`, `README.md:103` | Console messages with entity and brush counts; useful for parse errors. |
| Console print on map parse error | other | enhanced | `src/components/modules/main_module.cpp:359` (`map_parse_error_stub_01`) | Extra parse-error diagnostics in the console. |
| Undo/redo debug logging | undo | enhanced (diagnostic hooks only) | `src/components/modules/main_module.cpp:395-430` (`undo_general_start_stub`, `undo_undo_stub`), menubar Undo `menubar.cpp:225-227` calls stock `0x428730` | Undo/redo itself is stock; iw3xo adds console tracing. |
| Undo limit increased | undo | enhanced (claimed) | `README.md:102` "increased undo limit" | Longer undo history. Exact value not found in source: UNKNOWN. |
| Undo / Redo menu and hotkeys | undo | inherited (ImGui re-skin) | `src/ggui/menubar.cpp:225-232` (`cdeclcall 0x428730` / `0x428740`) | Standard Undo/Redo, stock behaviour. |
| Brush select / deselect / clone / delete / invert | selection | inherited (ImGui re-skin) | `menubar.cpp:1097-1105` (stock `0x4286xx` / `0x425690` family) | Familiar stock selection commands, now in a menu. |
| Select by targetname / classname / key-value / connected | selection | inherited (ImGui re-skin); implementation not checked | `menubar.cpp:1058-1076`, hotkeys `SelectionKeyValue` in `hotkeys.ini` | Menu entries for entity selection. Source for the selection logic: UNKNOWN. |
| Select complete tall / partial tall / touching / inside | selection | inherited (ImGui re-skin) | `menubar.cpp:1078-1095` | Stock select modes exposed as menu items. |
| Hide selected / unselected / show hidden / last hidden | selection | inherited (ImGui re-skin) | `menubar.cpp:486-500` (stock `0x42B6xx`), `hotkeys.ini` | Stock hide system with hotkeys. |
| Selection outline and tint draw toggles | selection | inherited (ImGui re-skin) | `menubar.cpp:509-515` (`dontDrawSelectedOutlines`, `dontDrawSelectedTint`), `hotkeys.ini` `ToggleOutlineDraw` | Toggles for how selected brushes are drawn. |
| Drag edges / drag vertices modes | selection | inherited (ImGui re-skin) | `menubar.cpp:1046-1056`, `hotkeys.ini` | Same drag modes as stock. |
| Brush primitives (cone, sphere, box, cylinder, arbitrary sided) | brush editing | inherited (ImGui re-skin) | `menubar.cpp:1675-1695` | Stock primitive brush creators in a menu. |
| CSG hollow / merge / auto caulk | brush editing | inherited (ImGui re-skin) | `menubar.cpp:1191-1206` (`cdeclcall 0x425570`, `0x4255D0`, `0x425600`) | Same stock CSG operations. |
| Clipper: toggle, clip selected, split selected, flip orientation | brush editing | inherited (ImGui re-skin) | `menubar.cpp:1208-1224` (`mainframe_thiscall 0x426510 / 0x427170 / 0x4271D0 / 0x427140`) | Stock clipper tool, now with menu entries. |
| Flip / rotate / scale (lock axes, free, arbitrary) | brush editing | inherited (ImGui re-skin) | `menubar.cpp:1111-1176` | Stock transform commands. |
| Free rotation and arbitrary rotation | brush editing | inherited (ImGui re-skin) | `menubar.cpp:1142-1148` | Stock rotation modes. |
| Extrude selected brush to a traced face | brush editing | added | `src/ggui/camera.cpp:776-865` (`game::Undo_GeneralStart("extrude to face")` at `camera.cpp:831`), `help.cpp:23`, `README.md:108` | Drag a brush toward another brush's face in the camera and it extends to meet it. |
| Make detail / structural / weapon clip / non-colliding / split coplanar | brush editing | inherited (ImGui re-skin) | `menubar.cpp:1300-1326` | Stock content-flag commands. |
| Patch: primitives (bevel, end cap, cylinders, cone) | patch editing | inherited (ImGui re-skin) | `menubar.cpp:1728-1768` | Stock patch primitive presets. |
| Patch: insert / add / delete rows and columns, matrix transpose | patch editing | inherited (ImGui re-skin) | `menubar.cpp:1795-1867` | Stock patch matrix edits. |
| Patch: redisperse, connect / weld, weld to model/brush/prefab verts | patch editing | inherited (ImGui re-skin) | `menubar.cpp:1855-1890` | Stock redisperse and weld commands. |
| Patch: split, invert, cap, cap texture, lock/unlock verts | patch editing | inherited (ImGui re-skin) | `menubar.cpp:1893-1940` | Stock patch operations. |
| Curve patch / terrain patch / cap / thicken dialogs (ImGui) | patch editing | enhanced | `src/ggui/patch.cpp:10` (Curve), `:125` (Terrain), `:220` (Thicken), `:296` (Cap) | The patch dialogs are redrawn in ImGui. |
| Terrain patch thickening | patch editing | added | `src/components/modules/pmesh.cpp:117` (`thicken_terrain_patch`), `pmesh.cpp:279`, `pmesh.cpp:296`, `README.md:105`, `hotkeys.ini` `ThickenPatch` | Give a terrain patch real thickness. |
| Vertex edit dialog with noise generator | patch editing | added (noise) / enhanced (dialog) | `src/ggui/vertex_edit.cpp:6` (`feature_noise`), `menubar.cpp:1579` (Noise Generator), `[rel] 340`, `README.md:90` | Random noise applied to selected vertices. |
| Advanced curve edit and vert-edit dialogs | patch editing | enhanced | `menubar.cpp:1785-1789`, `vertex_edit.cpp:76` | ImGui versions of the stock dialogs. |
| Stamp prefab | prefabs | added | `src/ggui/camera.cpp:618-640` (`stamp_prefab_imgui_imgui_menu`), `camera.cpp:553` (`rotate_stamped_prefab`), `README.md:106`, `[rel] 360` | Place a prefab in the camera with rotation and snapping. |
| Create prefab from selection | prefabs | UNKNOWN (README claim only) | `README.md:107`; no `create_prefab` symbol found in grep | Listed as a feature; implementation not located. |
| Enter / leave prefab | prefabs | inherited (ImGui re-skin) | `menubar.cpp:1225-1260`, stock `0x42BF70` / `0x42BF80` | Stock prefab edit mode from a menu. |
| Prefab browser with thumbnails and drag and drop | prefabs / asset browsers | added | `src/ggui/prefab_preview.cpp:123`, `src/components/modules/generate_previews.cpp` (thumbnails), `[rel] 369`, `menubar.cpp:349` | Browse prefabs, see thumbnails, drag into the scene. |
| Prefab physics bodies (dynamic collision for prefabs) | collision/physics | added | `src/components/modules/physx_impl.cpp:1044-1128` (prefab handling), `assets/map_source/prefabs/_iw3xo/dynamic_collision_bmodels.map`, `[rel] 372` | Prefabs can carry PhysX collision and be placed dynamically. |
| Model browser with preview and drag and drop | asset browsers (model) | added | `src/ggui/modelselector.cpp:53`, `menubar.cpp:345`, `README.md:79` | Browse and preview xmodels, drop them in the map. |
| Model previewer planting buttons | asset browsers (model) | added | `[rel] 352` (plant models / plant orient to floor icons in `assets/bin/IW3xRadiant/images/`) | Models can be planted on the floor from the previewer. |
| Cycle preview models | asset browsers (model) | inherited (ImGui re-skin) | `menubar.cpp:1643` | Stock preview model cycle. |
| Reload XModels | asset browsers (model) | inherited (ImGui re-skin) | `menubar.cpp:720` | Reloads models from the menu. |
| Textures / material browser: toolbar quick filter | materials | enhanced | `src/ggui/textures.cpp:730`, `src/common/texwnd.cpp` | Filter bar above the texture grid. |
| Texture favourite lists | materials | added | `src/common/texwnd.cpp:4` (`texwnd_vector_of_favourites`), `texwnd.cpp:18` (`add_selected_to_favourite_list`), `[rel] 369` | Custom favourite lists, like customizable texture filters. |
| Sky materials previewed in texture browser | materials | added | `README.md:98` (sky preview), renderer sky handling in `src/components/modules/renderer.cpp` | Sky textures are visible in the browser instead of blank. |
| Invalid texture / invalid material placeholder | materials | added | `assets/bin/IW3xRadiant/materials/invalid_material`, `assets/bin/IW3xRadiant/images/invalid_texture.iwi`, `README.md:91` | Missing materials show a clear placeholder. |
| Reload images / load new materials | materials | inherited (ImGui re-skin) | `menubar.cpp:712-716`, `menubar.cpp:1470-1474`, stock `game::R_ReloadImages` | Stock reload commands. |
| Reload individual materials without restart | materials | added | `[rel] 400` ("per-material reloading"), `src/components/modules/renderer.cpp:4165` (R_ReloadImages rewrite for jpg) | Reload one material while Radiant stays open. |
| Texture resolution and filter menus | materials | inherited (ImGui re-skin) | `menubar.cpp:622-705` (`r_texFilterMipMode`, picmip dvars) | Stock texture filter and picmip controls. |
| Layers and layer material window | materials | enhanced | `src/ggui/layers.cpp:334`, `src/common/layermatwnd.cpp`, `[rel] 369` (layer dialog moved into toolbox) | Layers window (also reachable from toolbox). |
| Texture lock (moves, rotations, lightmaps) | materials | inherited (ImGui re-skin) | `menubar.cpp:1437-1450` | Stock texture lock toggles. |
| Surface inspector | materials | enhanced | `src/ggui/surface_inspector.cpp:780`, `[rel] 322` ("experimental surface inspector") | Redrawn surface inspector. |
| Surface type / usage / locale filters | materials | inherited (ImGui re-skin) | `menubar.cpp:1504-1570` | Stock texture window filters. |
| Effects browser with preview and drag and drop | asset browsers (fx) | added | `src/ggui/effects_browser.cpp:113`, `src/common/fxwnd.cpp`, `menubar.cpp:939`, `[rel] 400` | Browse and preview effects, drop them in the scene. |
| Effect playback (play, pause, stop, repeat) | FX preview | added | `src/components/modules/effects.cpp:44` (`FX_SpawnEffect` wrapper), `src/fx_system/fx_system.cpp`, `src/fx_system/fx_cmd.cpp`, `[rel] 325` | Effects play inside the editor camera. |
| Effects editor (elements, properties, physics, lights, decals) | FX preview | added | `src/ggui/effects_editor_gui.cpp:236`, `:2849`, `src/components/modules/effects_editor.cpp`, `[rel] 330` | A full in-editor effect editor. |
| Export effects as CreateFX | FX preview / packaging | added | `src/fx_system/fx_save.cpp`, `src/components/modules/effects.cpp:580` (`write_fx_def_for_prefab`), `d3dbsp.cpp:1035` (`bsp_gen_createfx_on_compile`), `README.md:70` | Writes CreateFX files from the editor. |
| Effect xmodel drawing and effect sound entries | FX preview | added | `[rel] 330`, `src/components/modules/effects.cpp:662-669` (`is_sound` / `soundalias` epairs) | Effects can draw models; sound entries are read from entity keys. |
| Effect loop and sound options in createfx | FX preview | added | `[rel] 411`, `[rel] 411` wiki changelog | New createFX loop and sound options. |
| Effects framework `fx_origin` entity | FX preview | added | `[rel] 325` (wiki changelog) | An entity type for effect origin. |
| Lighting preview (light preview toggle, start/stop, clear, regions) | lighting preview | inherited (ImGui re-skin) with added state handling | `menubar.cpp:856-909`, `menubar.cpp:900` (`preview_at_max_intensity`) | Stock light preview controls in a menu. |
| Sun preview | lighting preview | enhanced | `src/components/modules/renderer.cpp:1610-1648` (`sunpreview_*`), hook `renderer.cpp:4092` (`sunlight_preview_arg_check`), `[rel] 330` ("re-implemented sun preview shadows") | Sun preview with iw3xo's shadow path. |
| Sun preview shadows enable and shadow distance limit | lighting preview | added | `menubar.cpp:866`, `README.md:83` (limit shadow drawing distance) | Shadow drawing distance can be limited for FPS. |
| Sun direction visualizer and debug | lighting preview | added | `menubar.cpp:875` (`game::glob::debug_sundir`), `src/game/functions.cpp` `debug_sundir` (glob), `[rel] 352` | Draws the sun direction in the editor. |
| Custom lighting shader (normal mapping, specular, reflections, fog) | lighting preview | added (shader path replaced) | `src/components/modules/renderer.cpp:705` (`set_custom_pixelshader_constants`), `renderer.cpp:1218-1600` (pass setup), `README.md:82` | The preview lighting is iw3xo's shader, not stock. |
| Fake sun preview, fog, settings | lighting preview | enhanced | `menubar.cpp:580-590`, dvars `r_fakesun_preview`, `r_fakesun_fog_enabled`, `src/common/layermatwnd.cpp:22` | Fake-sun lighting for the editor. |
| Normal-based / view-based fake lighting, fullbright, wireframe, case textures | lighting preview | inherited (ImGui re-skin) | `menubar.cpp:597-613` (`RM_*` draw modes, `src/game/structs.hpp:104-108`) | Stock draw modes. |
| Water rendered instead of case textures | lighting preview | enhanced | `menubar.cpp:555-560`, `src/components/modules/renderer.cpp:1442` (`r_drawWater`), `README.md:85` | Water shows as water in the editor. |
| Filmtweaks toggle and settings | lighting preview | added | `menubar.cpp:563-575`, dvars `r_filmtweak*` `src/game/dvars.cpp:261-266`, `[rel] 322` | Post-look color grading preview. |
| Post-effects support (postfx) | lighting preview | added | `[rel] 322`, `src/components/modules/renderer.cpp` (`renderer::postfx`, see `gui.cpp:294`) | Post-processing in the camera. |
| Reflection probes: generate, export, debug | lighting preview | added | `src/components/modules/reflectionprobes.cpp:574` (`R_GenerateReflectionRawData`), `:801`, `d3dbsp.cpp:1023` (`bsp_gen_reflections_on_compile`), `gui.cpp:424` (debug window), `README.md:74` | Cubemap reflections generated from the editor view. |
| Draw d3dbsp / toggle bsp vs radiant world | lighting preview / compilation | added | `src/components/modules/d3dbsp.cpp:1041` (`r_draw_bsp`), `menubar.cpp:1004-1014` | Compare the compiled BSP with the editor's brush view. |
| Load d3dbsp into the editor | compilation (bsp) | added | `src/components/modules/d3dbsp.cpp` (module, 1186 lines), `menubar.cpp:977-990`, `[rel] 360`, `README.md:73` | Compiled BSP can be loaded and viewed in Radiant. |
| Dynamic model entities in BSP | compilation (bsp) | added | `d3dbsp.cpp:661-690` (script models loaded first; load limit 512 log at `d3dbsp.cpp:690`), HEAD commit title "feat/bsp-dynamic-models" | Model entities in the BSP are loaded as dynamic objects. |
| Compile current map (bsp / light / options) | compilation (bsp) | added wrapper over stock compilers | `src/components/modules/d3dbsp.cpp:828-905` (`compile_bsp`), `assets/bin/IW3xRadiant/batch/compile_bsp.bat` (calls `bin\cod4map` and `bin\cod4rad`), `menubar.cpp:1027` | Compile button runs the stock bsp and light compilers with options set in the UI. |
| Compile settings dialog (bsp and light flags) | compilation (bsp) | added | `d3dbsp.cpp:1005-1060` (dvars), `menubar.cpp:1036` | UI for compiler switches (onlyents, samplescale, fast, extra, modelshadow, traces). |
| Show bsp after compile / auto reflections after compile | compilation (bsp) | added | `d3dbsp.cpp:1017`, `d3dbsp.cpp:895-905` (post-process callback) | Option to open the compiled bsp straight away. |
| Compile fastfile from the editor | packaging | added wrapper over stock linker | `src/components/modules/d3dbsp.cpp:781-820` (`compile_fastfile`), `assets/bin/IW3xRadiant/batch/compile_ff.bat` (calls `linker_pc.exe`), `d3dbsp.cpp:1029` (`bsp_gen_fastfile_on_compile`), `README.md:73` | Build a .ff from the editor, optionally after bsp compile. |
| Live-link devmap after fastfile | packaging / live link | added | `d3dbsp.cpp:796-812` (sends `devmap` dvar to the game) | Game reloads the map after the fastfile builds. |
| Process runner for batch jobs with spinner and output | compilation | added | `src/components/modules/process.cpp`, `src/components/modules/exec.cpp`, `d3dbsp.cpp:867-874` | Compiler output shown in a console window with a spinner. |
| Maya export | packaging | inherited (ImGui re-skin) | `src/ggui/menubar.cpp:1185-1186` (stock `0x4263C0`) | Stock Maya export command. |
| Save / open / save-as / new / project settings | packaging (file) | inherited (ImGui re-skin) | `menubar.cpp:64-215` (stock `0x423AA0`, `0x423AE0`, `0x423B80`, `0x423BC0`, `0x428DE0`) | Stock map file commands, now in a menu. |
| Open recent | packaging (file) | inherited (ImGui re-skin) | `menubar.cpp:93-110` | Stock MRU list. |
| Generate file: save selected / region / createfx / pointfile / errorfile | packaging (file) | inherited (ImGui re-skin) | `menubar.cpp:143-186` | Stock file-generation commands. |
| Live link: sync brushes (with collision), camera, worldspawn settings to game | live link | added | `src/components/modules/remote_net.cpp` (1105 lines), `:841-874` (`track_worldspawn_settings`, sun direction/color/light), `:549` (`cmd_send_single_brush`), `:522` (`cmd_send_camera_update`), `README.md:75`, `[rel] 372` | Brush edits and camera move in the game while Radiant is open. |
| Live link transport (TCP, default port 3700) | live link | added | `src/components/modules/remote_net.cpp:71-105` (socket, `radiant_livePort`, default `htons(3700)` at `:93`), `radiant_live` dvar `:1037` | Local TCP connection between editor and game. |
| Live link command set | live link | added | `src/game/structs.hpp:70-99` (`RADIANT_COMMAND_*`, `SERVER_*`), `remote_net.cpp:243` (camera update from server) | Selection, brush and camera messages both ways. |
| Live link: send dvar to game (SET_DVAR) | live link | added | `remote_net.cpp:488` (`Cmd_SendDvar`), `src/game/structs.hpp:84` | Dvars can be pushed to the running game. |
| Live link: game-side server code | live link | UNKNOWN | Not in this repo (game side is stock CoD4 + external component) | Where the game receives and applies the commands: UNKNOWN from source here. |
| Camera window: realtime viewport, combined view toggle | camera | enhanced | `src/common/camwnd.cpp:431-451` (hooks on `cam_rotate`, `cam_positiondrag`, `move_selection_stub`, detours for mouse handlers), `src/game/functions.cpp` `ccamwindow_realtime` glob, `[rel] 317` (viewports) | Camera redrawn with iw3xo viewports and mouse handling. |
| Camera window ImGui (render target, hover, toolbar) | camera | enhanced | `src/ggui/camera.cpp:1444` (`Camera Window##rtt`), `src/ggui/camera_settings.cpp:630` | Camera panel is an ImGui window with its own toolbar. |
| Camera context menu and stamp/extrude actions | camera | added | `src/ggui/camera.cpp:618` (stamp), `:776` (extrude), `[rel] 350` | Right-click actions in the camera window. |
| Camera guizmo (translate, rotate, scale) with world/local toggle | camera / selection | added | `src/ggui/camera_guizmo.cpp`, `src/common/camwnd.cpp:383-389` (`guizmo_enable`, `guizmo_snapping`), `menubar.cpp:1180`, `[rel] 400` (world/local) | 3D gizmo to move entities and brushes precisely. |
| Camera settings toolbar | camera | added | `src/ggui/camera_settings.cpp:630`, `camwnd.cpp:401` (`gui_camera_toolbar_defaultopen`), `camwnd.cpp:408` | Toolbar settings for the camera. |
| Center camera on selection / center 2D on camera / up-down floor | camera | inherited (ImGui re-skin) | `menubar.cpp:441-461`, `hotkeys.ini` | Stock view commands. |
| Zoom to cursor | camera / grid | added | `src/common/cxywnd.cpp:390`, `:464` (`grid_zoom_to_cursor`), dvar `:717`, `README.md:92` | Zoom centers on the mouse cursor. |
| Grid window (XY, YZ, XZ views, next view) | grid | inherited (ImGui re-skin) | `menubar.cpp:463-481`, `src/ggui/grid.cpp:550` | Stock 2D views shown in ImGui. |
| Grid sizes (0.5 through 512) | grid | inherited (ImGui re-skin) | `menubar.cpp:1329-1371`, `hotkeys.ini` `SetGrid*` | Same stock grid sizes, with a 128 entry. |
| Snap to grid (map and guizmo) | grid / snapping | inherited (map); added (guizmo snap) | `menubar.cpp:1375-1383`, `camwnd.cpp:389` (`guizmo_snapping`) | Map snapping is stock; guizmo snapping is iw3xo. |
| New-patch dragging | grid / patch | added | `src/common/cxywnd.cpp:729` (`grid_new_patch_drag`), `menubar.cpp:1385` | Option to drag new patches with the grid. |
| Grid edge and block coordinates, show names/blocks/angles/connections | grid | enhanced (dvars) | `cxywnd.cpp:723` (`grid_draw_edge_coordinates`), `menubar.cpp:517-546` | Extra grid labels; the rest are stock flags. |
| Context-aware grid context menu | grid | enhanced | `src/components/modules/gui.cpp:829` (`gui_grid_context_show_select`), `[rel] 340`, `[rel] 350` | Right-click in the grid shows a contextual menu. |
| Entity dialog (ImGui) with hover highlight in info window | entities | enhanced | `src/ggui/entity.cpp:1973`, `src/ggui/entity_info.cpp:69`, `[rel] 411` | Entity editing in ImGui; hover highlights entities in the info window. |
| Entity info window | entities | enhanced | `entity_info.cpp:69`, `menubar.cpp:253` | Entity info in ImGui. |
| Entity keyvalue editing / select connected | entities | enhanced / inherited | `src/ggui/entity.cpp` (epair handling), `menubar.cpp:1282-1292` (connect / link / ungroup) | Connect and link entities from the menu. |
| Entity color dialog and entity show modes | entities | inherited (ImGui re-skin) | `menubar.cpp:1618`, `menubar.cpp:827-853` (`ENTITY_*` modes, `structs.hpp:3741`) | Stock entity display modes (bounding box, wireframe, skinned). |
| Draw model origins | entities | added (dvar) | `menubar.cpp:525` (`r_draw_model_origin`), `renderer.cpp:145-177` (`render_origin_boxes_stub`) | Model origin boxes drawn in the editor. |
| Filter dialog (ImGui) with entity / brush filters | entities | enhanced | `src/ggui/filter.cpp:500`, `menubar.cpp:300-301` | Filter window in ImGui. |
| Script group, vehicle group, dyn entity, models replace dialogs | entities | inherited (ImGui re-skin) | `menubar.cpp:1622-1640` | Stock dialogs in a menu. |
| Entity spawnvars parser | entities | added | `src/utils/spawnvars.cpp`, `src/components/modules/d3dbsp.cpp:654` (script models), `src/components/modules/main_module.cpp:381` | Parses entity strings for BSP and debug output. |
| Mesh painter (xmodels/prefabs, random size, angles, weight) | other (asset painting) | added | `src/components/modules/mesh_painter.cpp` (702 lines), `src/ggui/mesh_painter_gui.cpp:16`, `src/common/zwnd.cpp:74`, `menubar.cpp:309` and `:1583`, `[rel] 400`, `README.md:76` | Paint models onto surfaces with per-object settings. |
| Rope / wire generator | brush editing / other | added | `src/ggui/rope_generator.cpp:265`, `menubar.cpp:1575`, `[rel] 352` | Generates rope/wire geometry. |
| Lock / unlock patch verts, patch wireframe modes | patch editing | inherited (ImGui re-skin) | `menubar.cpp:798-824`, `hotkeys.ini` | Stock patch display and lock modes. |
| Backface wireframe for patches | patch editing | added (dvar) | `menubar.cpp:801`, `renderer.cpp:178-216` | Extra patch display mode. |
| Invert / reverse curve texture | patch editing | inherited (ImGui re-skin) | `menubar.cpp:1906-1907`, `hotkeys.ini` | Stock texture inversion. |
| Vertex / brush physics preview: PhysX movement | collision/physics | added | `src/components/modules/physx_impl.cpp` (1995 lines), `src/common/physx/cct_controller.cpp`, `src/common/physx/cct_camera.cpp`, `menubar.cpp:1589`, `[rel] 377`, `README.md:72` | Character movement inside the editor with cod4-style bouncing. |
| PhysX physics-enabled effects | collision/physics | added | `physx_impl.cpp:1773` (`FilterShader`), `[rel] 372`, `README.md:71` | Effects can have physics and become misc_models. |
| PhysX debug visualization and camera sensitivity | collision/physics | added | `physx_impl.cpp:1849` (`physx_debug_visualization_box_size`), `:1857` (`physx_camera_sensitivity`) | Debug and tuning dvars. |
| Collision model for the BSP (planes, verts, tris) | collision/physics | enhanced | `src/components/modules/d3dbsp.cpp:421-543` (`CMod_LoadPlanes`, `CMod_LoadCollisionVertsAndTris`) | BSP collision loaded for in-editor use. |
| Terrain patch thickening (collision) | collision/physics | added | `pmesh.cpp:117`, `README.md:105` | Thick terrain patches. |
| Make non-colliding / weapon clip | collision/physics | inherited (ImGui re-skin) | `menubar.cpp:1308-1316` | Stock collision flags. |
| Surface-type / usage collision filters | collision/physics | inherited (ImGui re-skin) | `menubar.cpp:1550-1570` | Stock surface types. |
| Cubic clipping | camera / other | inherited (ImGui re-skin) | `menubar.cpp:923` (`m_bCubicClipping`), `src/common/cxywnd.cpp` | Stock cubic clip flag; no iw3xo-specific change located. |
| Assets: bo3 tool textures (optional) | materials | added | `assets/bo3-tool-textures.zip`, `README.md:104,125` | Optional tool textures pack. |
| Increased asset limits / high poly models | other | enhanced (claimed) | `README.md:86`; the 512 load log at `d3dbsp.cpp:690` is the only limit found | Higher model counts. Exact limit change: UNKNOWN. |
| Render actual water | lighting preview | enhanced | see "Water rendered instead of case textures" above | Water surfaces drawn as water. |
| Realtime viewports | camera | enhanced | see "Camera window: realtime viewport" above | Continuous camera redraw option. |
| Gameview (in-editor game view) | other | added | `src/components/modules/gameview.cpp` (507 lines), dvar `radiant_gameview` `gameview.cpp:464`, `menubar.cpp:293` | Shows the game view inside Radiant. |
| Generate previews (prefab and model thumbnails) | prefabs / asset browsers | added | `src/components/modules/generate_previews.cpp`, `[rel] 369` | Background thumbnail generation. |
| Sound browser | asset browsers (sound) | UNKNOWN | No sound browser window or ImGui `Begin` for sound found; `effects.cpp:662-669` only reads `soundalias` epairs | Whether a sound browser exists: UNKNOWN. |
| Layers in the toolbox | UI/layout | added | `[rel] 411` | Layers window integrated into toolbox. |
| Toolbox (brush and patch tools, camera toolbar integration) | UI/layout | added | `src/ggui/toolbox.cpp:1310`, `[rel] 369`, `menubar.cpp:353`, `camwnd.cpp:408` | Sidebar of tools. |
| ImGui fonts setup (Font Awesome icons) | UI/layout | added | `src/components/modules/gui.cpp:17` (`imgui_init_fonts`), `gui.cpp:42` | Icon-based toolbars and menus. |
| Prefs stored in profile and config | other | added | `src/components/modules/config.cpp`, `src/common/radiantapp.cpp:108,131`, `[rel] 369` (config files renamed, breaking) | Preferences saved in new config locations. |

---

## 3. Release tags and headline features

Source: `https://github.com/xoxor4d/iw3xo-radiant/releases` pages 1 and 2 (17 releases total) and the wiki Changelog. `gh release list` was not available.

- `1.0` (Initial Release): initial release, notes not informative.
- `232` (Initial Release): initial release, notes not informative.
- `236`: performance fix, packet crafting disabled while live-link is inactive.
- `317`: viewports merge.
- `322`: post-effects, filmtweak toggle, experimental surface inspector, shader dump command.
- `325`: effects framework based on effectsEd, `fx_origin` entity, `.efx` playback, createFX generation.
- `330`: full effects editor (physics, lights, decals), effect xmodel drawing, sun shadows re-implemented, Discord rich presence.
- `331`: hotfix for an effects editor crash on prefab enter or leave.
- `340`: Dear ImGui upgrade, ImGui camera, grid and vertex edit dialogs, vertex noise.
- `350`: new file dialogs, update check, grid context menu changes, GUI refactor.
- `352`: rope generator, sun direction debug preview, model previewer planting buttons, noise generator.
- `360`: d3dbsp loading and compile from Radiant, automatic reflections, prefab stamping, extrude to faces.
- `369`: toolbox, prefab browser with thumbnails, layer dialog, texture favourites; config files renamed (breaking).
- `372`: NVIDIA PhysX for physics effects and dynamic prefabs.
- `377`: in-editor character movement with PhysX collision; prefab browser search.
- `400`: mesh painter, effect browser, per-material reload, world/local gizmo, sky preview.
- `411`: entity hover highlight in info window, layers in toolbox, createFX loop and sound options. Latest release per wiki (28 May 2023).

The develop head (`7c3a409`) is a commit after the 411 release line, so features in the develop branch beyond 411 are not described by any release. Their exact list: UNKNOWN from release data (the HEAD commit title mentions "feat/bsp-dynamic-models").

---

## 4. UNKNOWN / could not determine

- Release notes: exact text for 1.0, 232 and 317 (near-empty); all `[rel]` claims are secondhand summaries.
- `gh` CLI not installed; no `gh release view` raw output.
- Create prefab from selection: no implementation symbol found (README claim only).
- Sound browser: no evidence it exists.
- Undo limit value: "increased" claimed in README but the number not found in source.
- Asset limit increases beyond the 512 dynamic model log: exact changes not located.
- Live link game-side code: not in this repo.
- Select by key/value, select connected, select touching/inside, terrain patch presets ("Curve To Terrain", "Faces To Terrain"): menu wrappers found, implementation not read.
- Gameview implementation details: module located, behaviour not traced.
- Full stock address table: `functions.hpp` address count (about 133) not fully mapped to each feature.
- Whether `deps/imgui`, `deps/physx`, `deps/ImGuizmo` etc. are populated in this checkout (submodules; directory listing exists but contents not verified).
- Launcher `assets/bin/IW3xRadiant.exe`: binary, source not available.
- Discord, update-check and WinHttp behaviour: located, not run.

