# MW2019 1.20.4.7623265-replay: runtime entry points for a live map link

Target: `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`, MD5 `1c238fe327f2ecc3b0db924c5b425439` (matches the brief).
Collected 2026-11-08. Read-only pass: symbol map grep, proxy source reads, odin sqlite (`mode=ro`). IDA was not opened, the game was not run, and no file was written except this one.

Status legend:
- **USED-BY-PROXY**: a hook, call, or byte check in `mw120rproxy` already depends on this address.
- **NAMED-ONLY**: the name appears in the map or another symbol file. The proxy does not call or hook it.
- **UNKNOWN**: not found, or the sources disagree. No address was guessed.

Abbreviations used in Evidence: `MAP` = `E:\IW8\Binaries\1.20-replay\game_dx12_ship_replay.exe.map` (line numbers are the map's own lines). `PX` = `E:\mw124\mw120rproxy\mw120rproxy\` (proxy source). `ZT` = `E:\mw124\mw120rproxy\iw8-zonetool\`. `IDA-CMF` = `E:\mw124\mw120rproxy\_ida_mw120_custom_map_flow.txt`. `IDA-SEAM` = `E:\mw124\mw120rproxy\_ida_mw120_seams.txt`.

## 0. Build identity and address convention

| Item | Value | Evidence |
|---|---|---|
| Image base | `0x140000000` | PE optional header ImageBase (read from the first 4 KB of the exe); `MAP:14-15` (`__ImageBase`, `unk_140000001`); `IDA-CMF:1` (`IMAGE_BASE`) |
| Entry RVA | `0x21CDC10` | PE optional header |
| PE sections (RVA) | `.text` 0x1000, `.rdata` 0x2351000, `.data` 0x29B1000, `.pdata` 0x12F35000, `_RDATA` 0x13098000, `.rsrc` 0x130A4000, `.reloc` 0x1320C000 | PE section table |
| Map segment mapping | map seg `0001` = `.text`, `0002` = `.rdata`, `0003` = `.data`. RVA = section RVA + map offset | `MAP:2-8` shows `.text` as `0002`; PE header proves `.text` is section 1. Cross-check: `DB_FindXAssetHeader` map offset `0x11A9890` gives RVA `0x11AA890`, identical to `IDA-CMF:478` and `PX/replay_bindings.h:76-78` |
| VA | `0x140000000 + RVA` | Standard |
| Proxy RVA remap | None. The exe has no `.rplmap` section, so `replaycompiler::Address()` returns native RVAs | `PX/replay_compiler_target.h:47-51` (looks for `.rplmap`); PE section list above |
| File locations | Exe is under `Builds\1.20-replay\`. `Binaries\1.20-replay\` holds only the `.map`, `.i64`, and odin-related sqlite | Directory listing |

Not used, and why:
- `E:\IW8\Binaries\1.20-replay\...` `game_dx12_ship_replay.exe.map` was the only symbol source for names. `_ida_mw120_dev_names.txt` has `IMAGE_BASE=0x0` and names a different database, so it was ignored.
- `E:\odin-symbols\game.sqlite3` is the game-test source database. Its RVAs do not match this exe (`DB_FindXAssetHeader` there is `0x1196630`, here `0x11AA890`). Ignored.
- `E:\odin-symbols\base.sqlite3` `func.rva` and `state.sqlite3` `sent.name` agree with the map for every name checked (about 45). `game_rva` and `game_name` in `base.func` refer to the game-test binary, so they were not used.

## 1. Asset type enum (XAssetType ordinals)

The ordinals come from the zonetool header, which says "retail-confirmed" (`ZT/src/zonetool/iw8/iw8_structs.h:7-8`). They are not verified in this binary. Proxy code independently uses several of them (noted below), which supports them.

| Type | Enum name (iw8_structs.h line) | Loader symbol (RVA) | Evidence | Status |
|---|---|---|---|---|
| 8 | `ASSET_TYPE_XMODELSURFS` (16) | `Load_XModelSurfs` `0xE2A7D0` (VA `0x140E2A7D0`) | `MAP` publics; `ZT/docs/LOADER_DISCOVERY.md:103` (type-8 route `0xDBDCC0 -> 0xE2A970 -> 0xE2A7D0`) | NAMED-ONLY |
| 9 | `ASSET_TYPE_XMODEL` (17) | `Load_XModel` `0xE29820` (VA `0x140E29820`) | `MAP:297434` | NAMED-ONLY |
| 11 | `ASSET_TYPE_MATERIAL` (18) | `Load_Material` `0xE0C1C0` (VA `0x140E0C1C0`) | `MAP` publics (seg 0001 off `0xE0B1C0`); proxy compares `type == 11` at `PX/custom_images.cpp:261,271` | USED-BY-PROXY (type value) / NAMED-ONLY (loader) |
| 19 | `ASSET_TYPE_IMAGE` (26) | `Load_GfxImage` `0xE2FD20` (VA `0x140E2FD20`); pixel loader `Load_ImagePixels` `0x19387D0` | `MAP:298045`; `ZT/docs/LOADER_DISCOVERY.md:106` (type-19 body `0xE2FD20`); proxy `PX/custom_images.cpp:219,261,271` | USED-BY-PROXY (value and `Load_ImagePixels`) |
| 23 | `ASSET_TYPE_COL_MAP` (29) (clipMap_t) | `Load_clipMap_t` `0xE0FD30` | `MAP:295477`; `PX/custom_physics.cpp:28` (type 23 gated on caller `0x1490AB0` = `WorldCollision_AddClipMap`) | USED-BY-PROXY (value) / NAMED-ONLY (loader) |
| 24 | `ASSET_TYPE_COM_MAP` (30) (ComWorld) | `Load_ComWorld` `0xDF27B0`; `Load_ComWorldPtr` `0xDF29D0` | `MAP:292241`; `ZT/docs/LOADER_DISCOVERY.md:149` | NAMED-ONLY |
| 25 | `ASSET_TYPE_GLASS_MAP` (31) (GlassWorld) | `Load_GlassWorldPtr` `0xDF3290` | `ZT/docs/LOADER_DISCOVERY.md:158` (route case 25) | NAMED-ONLY |
| 29 | `ASSET_TYPE_MAP_ENTS` (35) | `Load_MapEnts` `0xE0B520`; `Load_MapEntsPtr` `0xE0BD90` | `MAP:295115`; `PX/custom_physics.cpp:25` (type 29 gated on caller `0x1490C4F` = `WorldCollision_AddMapEnts`) | USED-BY-PROXY (value) / NAMED-ONLY (loader) |
| 30 | `ASSET_TYPE_FX_MAP` (36) (FxWorld) | `Load_FxWorld` `0xE3CF00` | `MAP:299430` | NAMED-ONLY |
| 31 | `ASSET_TYPE_GFX_MAP` (37) (GfxWorld) | `Load_GfxWorld` `0xD92E80` | `MAP:283289`; `ZT/docs/LOADER_DISCOVERY.md:167` | NAMED-ONLY |
| 41 | `ASSET_TYPE_LOCALIZE` (46) | `Load_LocalizeEntry` `0xE04780` | `MAP:294350`; proxy `PX/custom_images.cpp:249` (`type == 41`, `MW120R/MAP_*`) | USED-BY-PROXY (value) / NAMED-ONLY (loader) |
| 44 | `ASSET_TYPE_VFX` (47) (ParticleSystemDef) | `Load_ParticleSystemDefPtr` `0xD997D0` | `MAP:283747`. Case-to-body binding not verified | NAMED-ONLY |
| 51 | `ASSET_TYPE_RAWFILE` (50) | `Load_RawFile` `0xE04C70` | `MAP:294385` | NAMED-ONLY |
| 52 | `ASSET_TYPE_SCRIPTFILE` (51) | `Load_ScriptFile` `0xDFE370` | `MAP:293541`; `ZT/docs/LOADER_DISCOVERY.md:215` | NAMED-ONLY |
| 54 | `ASSET_TYPE_STRINGTABLE` (52) | `Load_StringTable` `0xE05520`; `Load_StringTablePtr` `0xE05660` | `MAP:294448`; `ZT/docs/LOADER_DISCOVERY.md:225-227`; proxy `PX/custom_images.cpp:255` (`mp/mapInfo.csv`) | USED-BY-PROXY (value) / NAMED-ONLY (loader) |
| 62 | `ASSET_TYPE_LUAFILE` (53) | `Load_LuaFile` `0xE04900` | `MAP:294361` | NAMED-ONLY |
| 63 | (not in the enum; `ScriptableMapEnts` case per doc) | `Load_ScriptableMapEnts` `0xDBCF30` | `MAP:286317`; `ZT/docs/LOADER_DISCOVERY.md:129` (case 63) | NAMED-ONLY |
| 43, 57 | Not in the enum | `DB_FindXAssetHeader_Type57` `0x14E1B70` (name only) | `PX/custom_weapons.cpp:683` (`type != 43 && != 54 && != 57`); `PX/custom_images.cpp:345` | UNKNOWN |

Table locations for the enum and loader dispatch (not read; use only for a later verification pass): `k_XAssetTypeSizeTable` at RVA `0x2458160` (VA `0x142458160`), `k_DB_XAssetTypeFuncs` at RVA `0x23FD3A0` (VA `0x1423FD3A0`) (`MAP:` seg 0002 names `k_XAssetTypeSizeTable`, `k_DB_XAssetTypeFuncs`).

## 2. Asset lookup and loaders

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Asset header lookup by type/name | `DB_FindXAssetHeader` | RVA `0x11AA890` / VA `0x1411AA890` | `MAP:382522`; `PX/replay_bindings.h:76-78` (`FindAsset`); hook `PX/custom_images.cpp:248` (`Lookup`) installed at `PX/custom_images.cpp:378-388`; `IDA-CMF:478` | USED-BY-PROXY |
| Asset enumeration | `DB_EnumXAssets` | RVA `0xD86B40` / VA `0x140D86B40` | `MAP:282088` | NAMED-ONLY |
| Load dispatcher | `Load_XAssetHeader` | RVA `0xDBDCC0` / VA `0x140DBDCC0` | `MAP:286392`; `ZT/docs/LOADER_DISCOVERY.md:103`; `PX/replay_symbols.h` (name only) | NAMED-ONLY |
| Preload dispatcher | `Preload_XAssetHeader` | RVA `0xDCBFD0` / VA `0x140DCBFD0` | `MAP:287856`; `ZT/docs/LOADER_DISCOVERY.md:117` | NAMED-ONLY |
| Postload dispatcher | `Postload_XAssetHeader` | RVA `0xDC65F0` / VA `0x140DC65F0` | `MAP:287301`; `ZT/docs/LOADER_DISCOVERY.md:125` | NAMED-ONLY |
| Link an asset into the zone table | `DB_LinkXAssetEntry` | RVA `0x11AC760` / VA `0x1411AC760` | `MAP:382725`; `PX/replay_bindings.h:104-106` (`LinkAssetEntry`); `PX/engine_diagnostics.cpp:264` | USED-BY-PROXY |
| Load-level assets (blocking) | `DB_LoadLevelXAssets` | RVA `0xF63750` / VA `0x140F63750` | `MAP:327560` | NAMED-ONLY |
| Load level by map name | `DB_LoadLevelFromMapName` | RVA `0xF63A20` / VA `0x140F63A20` | `MAP:327578` | NAMED-ONLY |
| "DB_LoadXAssets" (requested) | none | UNKNOWN | Map grep: no symbol. `DB_LoadLevelXAssets` is the nearest name | UNKNOWN |
| Fastfile load entry | `DB_LoadFastfiles` | RVA `0x11ACD30` / VA `0x1411ACD30` | `MAP:382764`; `PX/game.h:123-125`; hook `PX/custom_weapons.cpp:649` (`LoadWithWeapons`) | USED-BY-PROXY |
| Fastfile file open | `DB_File_OpenDBFile` | RVA `0xD8CA30` / VA `0x140D8CA30` | `PX/game.h:28`; `PX/fastfile_diagnostics.cpp:77` (observe hook) | USED-BY-PROXY (diagnostic) |
| Zone load by name | `DB_LoadZone` (name claimed by `PX/game.h:174`) | RVA `0x12279C0` in the proxy, but the map names this address `GEntity_TraceCapsuleOnPlatform` (`MAP:394197`) | Conflict between `PX/game.h:174` and `MAP:394197` | UNKNOWN |
| Fastfile header auth/decompress | `DB_CmpLoad_Decompress` (`PX/game.h:177` calls it `DB_AuthLoad_Inflate` / `DB_BlockDecode` in `replay_symbols.h`) | RVA `0xD89E10` / VA `0x140D89E10` | `MAP:282407`; `PX/game.h:177-185` (no hook) | NAMED-ONLY (name conflict, see section 9) |

Asset loader bodies (NAMED-ONLY, none hooked): `Load_Material` `0xE0C1C0`, `Load_XModel` `0xE29820`, `Load_GfxWorld` `0xD92E80`, `Load_ComWorld` `0xDF27B0`, `Load_clipMap_t` `0xE0FD30`, `Load_MapEnts` `0xE0B520`, `Load_MapEntsPtr` `0xE0BD90`, `Load_ScriptableMapEnts` `0xDBCF30`, `Load_RawFile` `0xE04C70`, `Load_ScriptFile` `0xDFE370`, `Load_LuaFile` `0xE04900`, `Load_LocalizeEntry` `0xE04780`, `Load_StringTable` `0xE05520`, `Load_FxWorld` `0xE3CF00`, `Load_FxCombinedDef` `0xE2FC30`, `Load_ParticleSystemDefPtr` `0xD997D0`.

Asset-loader gap: `Load_Image` does not exist as a name. The image body is `Load_GfxImage`.

## 3. Map load path

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Server spawn thread | `SV_SpawnServerThread` | RVA `0x132D370` / VA `0x14132D370` | `MAP:420227` | NAMED-ONLY |
| Devmap command (listen/local) | `SV_DevMap_f` | RVA `0x136BBB0` / VA `0x14136BBB0` | `MAP:426173` | NAMED-ONLY |
| Dedicated devmap command | `SV_DevMapDedi_f` | RVA `0x136BD70` / VA `0x14136BD70` | `MAP:426182` | NAMED-ONLY |
| Devmap start | `Com_GameStart_BeginDevmap` | RVA `0x10C8D10` / VA `0x1410C8D10` | `MAP:359952` | NAMED-ONLY |
| SP devmap command object | `SvGameModeAppSP::DevmapCmd` | RVA `0x1B52060` / VA `0x141B52060` | `MAP` publics (seg 0001 off `0x1B51060`) | NAMED-ONLY |
| Server MP map assets | `SV_InitMP_SpawnServer_LoadMapAssets` | RVA `0x137B010` / VA `0x14137B010` | `MAP:427384` | NAMED-ONLY |
| Client MP load map | `CL_CGameMP_LoadMap` | RVA `0x15DD690` / VA `0x1415DD690` | `MAP:479416` | NAMED-ONLY |
| Collision world load | `CM_LoadMap` | RVA `0x12954D0` / VA `0x1412954D0` | `MAP:404202` | NAMED-ONLY |
| SP level load | `G_MainSP_LoadLevel` | RVA `0x182D0B0` / VA `0x14182D0B0` | `MAP` publics | NAMED-ONLY |
| Map info list (stock menu) | `Com_GameInfo_LoadMapInfoList` | RVA `0x10C7E70` / VA `0x1410C7E70` | `MAP:359829`; hook `PX/custom_map_ui.cpp:81-84`; `PX/replay_bindings.h:174-176` (`LoadMapInfoList`) | USED-BY-PROXY |
| Map info count (global) | `s_gameInfoData_mapCount` | RVA `0xC4FD8B4` / VA `0x14C4FD8B4` | `MAP:1024111` (seg 0003 off `0x9B4C8B4`); `PX/custom_map_ui.cpp:18` (`CountRva`) | USED-BY-PROXY |
| Map info rows (global base) | unnamed (`unk_14C4FD8B8`) | RVA `0xC4FD8B8` / VA `0x14C4FD8B8` | `PX/custom_map_ui.cpp:18` (`ListRva`); `MAP` publics (unnamed) | USED-BY-PROXY |
| Map info row layout | proxy constants: stride `0x4D8`, capacity 128 | n/a | `PX/custom_map_ui.cpp:16` | USED-BY-PROXY |
| Map selection apply | console `set ui_mapname` / `set ui_gametype` | n/a | `PX/developer_ui.cpp:142-197` (`SelectPackage`, `RestoreStock`) | USED-BY-PROXY |

No hook in the proxy calls or hooks any of the spawn, devmap, or CM_LoadMap functions. The proxy reaches map load only through the stock lobby, after setting two UI dvars.

Map-load gap: the function that runs for `devmap <name>` in Local Play is not identified. `SV_DevMap_f` is the candidate, but its command registration site (`Cmd_AddCommandInternal` callers) was not traced.

## 4. Console and command execution

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Append command text (console entry) | `Cbuf_AddText` (proxy `AddConsoleText`) | RVA `0x15C1C20` / VA `0x1415C1C20` | `MAP:476451`; hook `PX/developer_ui.cpp:612-617` (`ConsoleText`, `PX/developer_ui.cpp:557`) | USED-BY-PROXY |
| Main command buffer global | `s_cbufMainBuffer` | RVA `0xD216D70` / VA `0x14D216D70` | `MAP:1028423` (seg 0003 off `0xA865D70`); `PX/replay_bindings.h:69`; `PX/developer_ui.cpp:84-113` (`Submit` writes `{char* data; int capacity=0x10000; int size}`, size at +0x0C) | USED-BY-PROXY |
| Critical section (index 35 = CBUF) | `Sys_EnterCriticalSection` / `Sys_LeaveCriticalSection` | RVA `0x13F54F0` / `0x13F55F0` | `MAP:440565,440574`; `PX/developer_ui.cpp:95-96` (`enter(35)`) | USED-BY-PROXY |
| Execute buffer | `Cbuf_Execute` | RVA `0x1295EE0` / VA `0x141295EE0` | `MAP:404267` | NAMED-ONLY |
| Command dispatch | `Cmd_ExecuteSingleCommand_Internal` | RVA `0x12976C0` / VA `0x1412976C0` | `MAP:404470` | NAMED-ONLY |
| Command registration | `Cmd_AddCommandInternal` | RVA `0x12965F0` / VA `0x1412965F0` | `MAP:404320` | NAMED-ONLY |
| Execute script file | `Cmd_ExecScriptFile` | RVA `0x1297580` / VA `0x141297580` | `MAP:404459` | NAMED-ONLY |
| String command dispatch (server, client 0) | `G_CmdsMP_StringCommandDispatch` (proxy `ClientCommand`) | RVA `0x120B6A0` / VA `0x14120B6A0` | `MAP:391679`; hook `PX/noclip.cpp:150-152`; `PX/replay_bindings.h:227-229` | USED-BY-PROXY |
| Argument fetch | `SV_Cmd_ArgvBuffer` (proxy `ServerArgv`) | RVA `0x1298B40` / VA `0x141298B40` | `MAP:404660`; `PX/noclip.cpp:32,143` | USED-BY-PROXY |
| Console command text translation | proxy `commandtext::Translate` | n/a | `PX/command_text.cpp` (readable dvar name to token); `PX/developer_ui.cpp:577` | USED-BY-PROXY (proxy-side) |

The proxy never hooks the `Cbuf_Execute` or `Cmd_ExecuteSingleCommand_Internal` path. Its command injection is: `ConsoleText` intercepts `Cbuf_AddText`, translates names, and writes the text straight into `s_cbufMainBuffer` (`PX/developer_ui.cpp:84-113`) on the main thread.

## 5. Dvars

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Find dvar by name | `Dvar_FindVarByName` | RVA `0x13E63A0` / VA `0x1413E63A0` | `MAP:438907` | NAMED-ONLY |
| Get string | `Dvar_GetStringSafe` (proxy `GetString`) | RVA `0x13E69B0` / VA `0x1413E69B0` | `MAP:438975`; `PX/replay_bindings.h:50-53`; `PX/developer_ui.cpp:72-77` (`DvarString`) | USED-BY-PROXY |
| Get bool | `Dvar_GetBoolSafe` (proxy `GetBool`) | RVA `0x13E6820` / VA `0x1413E6820` | `MAP:438951`; `PX/replay_bindings.h:54-57`; `PX/noclip.cpp:42` | USED-BY-PROXY |
| Get int | `Dvar_GetIntSafe` | RVA `0x13E6960` / VA `0x1413E6960` | `MAP:438970` | NAMED-ONLY |
| Set string by name | `Dvar_SetStringByName` | RVA `0x13E9A20` / VA `0x1413E9A20` | `MAP:439254` | NAMED-ONLY |
| Set bool by name | `Dvar_SetBoolByName` | RVA `0x13E8A10` / VA `0x1413E8A10` | `MAP:439156` | NAMED-ONLY |
| Set int by name | `Dvar_SetIntByName` | RVA `0x13E95E0` / VA `0x1413E95E0` | `MAP:439215` | NAMED-ONLY |
| Set from string (source) | `Dvar_SetFromStringByNameFromSource` | RVA `0x13E9330` / VA `0x1413E9330` | `MAP:439199` | NAMED-ONLY |
| Register bool (boot override hook) | `Dvar_RegisterBool` | RVA `0x13E7670` / VA `0x1413E7670` | `MAP:439065`; `PX/game.h:42-56`; hook `PX/dvar_patches.cpp:341-349` | USED-BY-PROXY |
| Register variant (boot override / trace) | `Dvar_RegisterVariant` | RVA `0x13E7D40` / VA `0x1413E7D40` | `MAP:439094`; `PX/game.h:76-85`; hook `PX/dvar_patches.cpp:354-356` | USED-BY-PROXY |
| Name checksum | `Dvar_GenerateChecksum` | RVA `0x13E66C0` / VA `0x1413E66C0` | `MAP:438940`; `PX/game.h:91-94` (declared, not hooked) | NAMED-ONLY |

Dvar tokens used by the proxy (readable name to runtime token, `PX/dvar_names.h`, a table of 6727 entries):

| Readable name | Runtime token | dvar_names.h line | Proxy use | Status |
|---|---|---|---|---|
| `ui_mapname` | `NSQLTTMRMP` | 5026 | `PX/developer_ui.cpp:165,378,442,455,465` (read) and `:176,197` (set via console) | USED-BY-PROXY |
| `ui_gametype` | `MOLPOSLOMO` | 2481 | `PX/developer_ui.cpp:166,176,197` | USED-BY-PROXY |
| `systemlink` | `LPSPMQSNPQ` | 978 | `PX/noclip.cpp:42` (read by noclip); `PX/dvar_patches.cpp:264,305` | USED-BY-PROXY |
| `cg_viewedSplashScreen` | `MLNMPQOON` | 1959 | `PX/dvar_patches.cpp:243` | USED-BY-PROXY |
| `g_gametype` | `NKTMKRMSKR` | 3695 | none | NAMED-ONLY |
| `sv_running` | `NLTTMLSQRQ` | 3893 | none | NAMED-ONLY |
| `sv_cheats` | `NTPNRQTKNP` | 5197 | none | NAMED-ONLY |

Note: the proxy passes these tokens to the engine getters as-is (`PX/developer_ui.cpp:72-77`), and rewrites readable names to tokens for console input (`PX/command_text.cpp`). A live link should use the token form, or go through the same translation.

## 6. Entities, MapEnts, spawn

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Entity array / pointer global | `g_entities` | RVA `0xBC20F00` / VA `0x14BC20F00` | `MAP:1022391` (seg 0003 off `0x926FF00`); `PX/noclip.cpp:48` (reads the 8 bytes at this global as the local entity pointer) | USED-BY-PROXY |
| Client array global | `g_clients` | RVA `0xD1CFDA0` / VA `0x140D1CFDA0` | `MAP` publics (seg 0003) | NAMED-ONLY |
| Spawn-var parser | `Scr_ParseSpawnVars2` | RVA `0x12E0F50` / VA `0x1412E0F50` | `MAP:412071`; `PX/replay_symbols.h` (not hooked) | NAMED-ONLY |
| Entity spawn | `G_Utils_SpawnEntity` | RVA `0xFD8930` / VA `0x140FD8930` | `MAP:338127` | NAMED-ONLY |
| Generic `G_Spawn` / `G_ParseSpawnVars` / `G_SpawnEntitiesFromString` | none | UNKNOWN | Map grep: no exact symbol. Only `G_Spawn_*` family (for example `G_Spawn_info_volume` at seg 0001 off `0xFC3860`) | UNKNOWN |

Gclient layout used by the proxy, inferred from its own code (not symbols): `gentity + 0x150` = gclient pointer; `client + 0x0C` = `pm_type` (int); `client + 0x5DD0` = flags word (bit 0 = noclip) (`PX/noclip.cpp:48-50, 84-90, 100, 123-131`).

## 7. Player and camera

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Set origin (proxy name) | `G_ClientMP_SetPlayerStateFromNet` (proxy `SetOrigin`) | RVA `0x11F1DD0` / VA `0x1411F1DD0` | `MAP:388901`; `PX/replay_bindings.h:238-241`; call `PX/noclip.cpp:105` | USED-BY-PROXY (name conflict, section 9) |
| Set view angles (proxy name) | `G_ActiveMP_ApplyClientViewAngles` (proxy `SetViewAngles`) | RVA `0x11F1EE0` / VA `0x1411F1EE0` | `MAP:388906`; `PX/replay_bindings.h:244-249`; call `PX/noclip.cpp:108` | USED-BY-PROXY (name conflict) |
| Noclip flag test site | proxy `NoclipMovementFlags` (unlabeled, `loc_1411E801B`) | RVA `0x11E801B` / VA `0x1411E801B` | `PX/replay_bindings.h:235-237`; `MAP` publics `loc_1411E801B` | USED-BY-PROXY (instruction site) |
| Client view origin | `G_Client_GetViewOrigin` | RVA `0xFB5CA0` / VA `0x140FB5CA0` | `MAP:335234` | NAMED-ONLY |
| Linked view origin | `G_Client_GetLinkedViewOrigin` | RVA `0xFB5920` / VA `0x140FB5920` | `MAP` publics | NAMED-ONLY |
| Player view-angle update | `PM_UpdateViewAngles` | RVA `0x1128540` / VA `0x141128540` | `MAP:368654` | NAMED-ONLY |
| SP forward view-angle update | `G_ClientSP_ForwardUpdateViewAngles` | RVA `0x1035870` / VA `0x141035870` | `MAP` publics | NAMED-ONLY |
| Render world pointer (map state) | `rgp_world` | RVA `0x10C77870` / VA `0x150C77870` | `MAP` publics; `PX/custom_render.cpp:141-146` (reads world pointer; `world+0` = char* name; `world+0x4460` = tome) | USED-BY-PROXY |
| Local player noclip / set-position command | proxy `noclip`, `mw_noclip`, `mw_setpos x y z [yaw pitch]` | n/a | `PX/noclip.cpp:30-135`; requires `systemlink` true and an active Local Play match (`PX/noclip.cpp:42`) | USED-BY-PROXY |

No refdef or camera-matrix global was found. Camera for the renderer is UNKNOWN.

## 8. Debug draw

| Purpose | Symbol name | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Game debug line (single function) | `CgHandler::DebugLineAll` | RVA `0xC91FC0` / VA `0x140C91FC0` | `MAP:261176`; odin `state.sqlite3` `sent` name match | NAMED-ONLY |
| 3D debug registration | `RBTD_Register_DrawDebug3D` | RVA `0x34CE40` / VA `0x14034CE40` | `MAP:64016`; `RBTD_DrawDebug3D_NoOpCallback` at RVA `0x189E8B0` (`MAP` publics) | NAMED-ONLY |
| Entity info line | `G_Debug_EntInfoLine` | RVA `0x1212090` / VA `0x141212090` | `MAP` publics | NAMED-ONLY |
| Havok debug (engine, not game) | `hkDebugDisplay_drawLineArray` | RVA `0x1D24FA0` / VA `0x141D24FA0` | `MAP` publics | NAMED-ONLY |
| Immediate-mode line / box / sphere draw API | none | UNKNOWN | Map grep (`DebugBox`, `DebugSphere`, `AddDebug`, `CG_DrawDebug`): no clean game-side draw entry | UNKNOWN |

## 9. Physics and collision (map-relevant)

| Purpose | Symbol name (proxy name) | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Clip map collision add (caller site, type 23) | `WorldCollision_AddClipMap` (site `ClipShapeCall` 0x1490A94) | RVA `0x1490A90` / VA `0x141490A90` | `MAP:451064`; `PX/custom_physics.cpp:25-28`; `PX/replay_bindings.h:116-118` | USED-BY-PROXY |
| Map ents collision add (caller site, type 29) | `WorldCollision_AddMapEnts` (site `MapShapeCall` 0x1490C40) | RVA `0x1490C00` / VA `0x141490C00` | `MAP:451081`; `PX/custom_physics.cpp:25`; `PX/replay_bindings.h:112-114` | USED-BY-PROXY |
| Instantiate collision (proxy `WorldCollisionCreate`) | `WorldCollision_InstantiateCollision` | RVA `0x1491180` / VA `0x141491180` | `MAP:451115`; `PX/custom_collision.cpp:409-410`; `PX/replay_bindings.h:137-140` | USED-BY-PROXY |
| Shutdown collision (proxy `WorldCollisionShutdown`) | `WorldCollision_ShutdownCollision` | RVA `0x1491AC0` / VA `0x141491AC0` | `MAP:451170`; `PX/custom_collision.cpp:404-406` | USED-BY-PROXY |
| Havok shape list (proxy `AddShapeList`) | `HavokPhysics_AddShapeList` | RVA `0x161AB50` / VA `0x14161AB50` | `MAP:485312`; `PX/custom_physics.cpp:112-113` | USED-BY-PROXY |
| Set main shape list | `HavokPhysics_SetMainShapeList` | RVA `0x162A540` / VA `0x14162A540` | `MAP:486661`; `PX/custom_physics.cpp:110` | USED-BY-PROXY |
| Create compound shape | `HavokPhysics_CreateCompoundShape` | RVA `0x161C770` / VA `0x14161C770` | `MAP:485485`; `PX/custom_collision.cpp:400` | USED-BY-PROXY |
| Create convex hull shape | `HavokPhysics_CreateConvexHullShape` | RVA `0x161CBF0` / VA `0x14161CBF0` | `MAP:485515`; `PX/custom_collision.cpp:399` | USED-BY-PROXY |
| Create convex hull from AABB | `HavokPhysics_CreateConvexHullFromAABB` | RVA `0x161CFB0` / VA `0x14161CFB0` | `MAP:485528`; `PX/custom_collision.cpp:399` | USED-BY-PROXY |
| Instantiate static body | `Physics_InstantiateStaticBody` | RVA `0x108FF40` / VA `0x14108FF40` | `MAP:354448`; `PX/custom_collision.cpp:400` | USED-BY-PROXY |
| Destroy physics instance | `Physics_DestroyInstance` | RVA `0x108DDB0` / VA `0x14108DDB0` | `MAP:354274`; `PX/custom_collision.cpp:401` | USED-BY-PROXY |
| Remove Havok reference | `hkReferencedObject::removeReference` | RVA `0x1C8E480` / VA `0x141C8E480` | `MAP` publics; `PX/custom_collision.cpp:401` | USED-BY-PROXY |
| Omnvar tables clear / load | `BG_Omnvar_ClearTables` `0xCD5500`; `BG_Omnvar_LoadTables` `0xCD5C80` | VA `0x140CD5500`, `0x140CD5C80` | `MAP:267952,268015`; `PX/custom_omnvars.cpp:79-83` | USED-BY-PROXY |

## 10. UI, key input, file I/O, errors (proxy-hooked)

| Purpose | Symbol name (proxy name) | Address or RVA | Evidence (file:line) | Status |
|---|---|---|---|---|
| Key event (proxy `KeyEvent`) | `CL_Keys_Event` | RVA `0x15BEB80` / VA `0x1415BEB80` | `MAP:476061`; `PX/developer_ui.cpp:612-613` | USED-BY-PROXY |
| Console draw / toggle / active | `Con_DrawConsole` `0x15AE0B0`; `Con_ToggleConsole` `0x15B18C0`; `Con_IsActive` `0x15B0EF0` | VA `0x1415AE0B0`, `0x1415B18C0`, `0x1415B0EF0` | `MAP:474180,474426`; `PX/developer_ui.cpp:602-603` | USED-BY-PROXY |
| Overlay draw (proxy `Overlay`) | `ClGameModeApplicationMP::ClearWaitingForServer` (conflict) | RVA `0x15E1340` / VA `0x1415E1340` | `MAP:479791` (seg 0001 off `0x15E0340`); `PX/developer_ui.cpp:608-609`; bytes `8B CA E9` in `PX/replay_bindings.h:12` | USED-BY-PROXY (name conflict) |
| Print message (proxy `PrintMessage`) | `Com_PrintMessageInternal` | RVA `0x12B0660` / VA `0x1412B0660` | `MAP:407059`; `PX/engine_diagnostics.cpp:246` | USED-BY-PROXY |
| Com error (proxy `ComErrorInternal`) | `Com_Error_Internal` | RVA `0x12AB1C0` / VA `0x1412AB1C0` | `MAP:406456`; `PX/engine_diagnostics.cpp:255` | USED-BY-PROXY |
| Fatal text (proxy `FatalText`) | `Com_SendSysErrorToDLog` | RVA `0x12B36A0` / VA `0x1412B36A0` | `PX/engine_diagnostics.cpp:259`; `MAP` publics | USED-BY-PROXY (name conflict) |
| Disc error (proxy `DiscError`) | `DB_AsyncIWFileLoad_DirtyDiscError` | RVA `0xD86F00` / VA `0x140D86F00` | `MAP:282109`; `PX/engine_diagnostics.cpp:251` | USED-BY-PROXY |
| Disk open, selected package routing (proxy `DiskOpen`) | `Sys_OpenFileReliable` | RVA `0x13F4D50` / VA `0x1413F4D50` | `MAP:440508`; hook `PX/custom_map_loader.cpp:96-107` (`OpenSelected` at `:67`) | USED-BY-PROXY |
| Reader reset (proxy `ResetReader`) | `DB_AsyncIWFileLoad_ReadBufferFinish` | RVA `0xD871E0` / VA `0x140D871E0` | `MAP:282114`; hook `PX/custom_map_loader.cpp:111-112` (`ResetSelectedReader` at `:20`) | USED-BY-PROXY (name conflict) |
| Inflator init (proxy `InitInflator`) | `DB_AsyncIWFileLoad_RegisterHashCallback` | RVA `0xD88120` / VA `0x140D88120` | `MAP:282219`; called from `PX/custom_map_loader.cpp:16,101-105` (not hooked) | USED-BY-PROXY (call; name conflict) |
| FS read, current thread (opt-in monitor / redirect) | `FS_FOpenFileReadCurrentThread` | RVA `0x13D9240` / VA `0x1413D9240` | `MAP:437546`; `PX/game.h:166-171`; hook `PX/file_open_hook.cpp:43-51` (`Detour` at `:26`) | USED-BY-PROXY (off by default: `PX/mw120rproxy.ini` `custom_map_file_monitor=0`) |
| Map-exists GSC helper | `GScr_MapExists` | RVA `0x15E16B0` / VA `0x1415E16B0` | `MAP:479839`; `PX/game.h:189` (declared, not hooked) | NAMED-ONLY |

## 11. Proxy hooks relevant to maps

Install order (`PX/dllmain.cpp:62-92`): dvars, offline auth, engine diagnostics, fastfile diagnostics, `customloader` (DiskOpen + ResetReader), `customweapons` (DB_LoadFastfiles), `customomnvars`, `custommapui` (LoadMapInfoList), `noclip` (ClientCommand), `developerui` (Overlay, KeyEvent, AddConsoleText), `customimages` (FindAsset, installed last), `fileopen` (FS monitor/redirect, opt-in).

1. **Map list injection.** `custommapui::Load` (`PX/custom_map_ui.cpp:23-80`) runs after the stock `Com_GameInfo_LoadMapInfoList`. It reads the count from `0xC4FD8B4` and the rows from `0xC4FD8B8` (stride `0x4D8`, capacity 128). It clones the `mp_shipment` row and writes the custom package. Fields in the cloned row: `+0x00` title (first byte 31), `+0x20` id (up to 15 chars), `+0x30` description, `+0x50` and `+0x70` `mw120r/<id>`, `+0xB0` and `+0xD0` `war`, `+0x4D0` int `aliens = 0` (`PX/custom_map_ui.cpp:44-70`).
2. **Asset lookup hook (`DB_FindXAssetHeader`).** `customimages::Lookup` (`PX/custom_images.cpp:248-280`). Type 41 with `MW120R/MAP_*` returns a localized string. Type 54 `mp/mapInfo.csv` is rewritten. Types 19 and 11 with `loadscreen_mp_hackney` are redirected to the selected package's loadscreen. Installed at `PX/custom_images.cpp:378-388`.
3. **Selected package disk routing.** `OpenSelected` (`PX/custom_map_loader.cpp:67-94`) calls `custommaps::ResolveDiskRead` (`PX/custom_maps.cpp:562`) and `customweapons::ResolveDiskRead`, then redirects only canonical stock zone requests for the selected package. `ResetSelectedReader` (`PX/custom_map_loader.cpp:20-64`) accepts the unsigned `IWffc100` header (`header[8] == 11`, `0xF7 0x0F` at 12-13) for the selected zone, and calls `InitInflator` with kind 0.
4. **Package layout and naming.** Root `<game>\mods\mw120r\maps` (`PX/custom_maps.cpp:455`). Id must start `mp_` and be 4 to 15 chars (`PX/custom_maps.cpp:38`). Five zones per id: `<id>`, `srv_<id>`, `eng_<id>`, `ww_<id>`, `techsets_<id>` (`PX/custom_maps.cpp:274-276`). Optional `map.json` metadata (`PX/custom_maps.cpp:278-313`).
5. **Map selection.** `SelectPackage` (`PX/developer_ui.cpp:142-186`) sets `ui_mapname` = package id and `ui_gametype` = `war` through `Submit("set NSQLTTMRMP ...\nset MOLPOSLOMO war\n")`. `RestoreStock` (`PX/developer_ui.cpp:188-205`) restores the previous values. Only allowed in the Local Play lobby (`CanSelect`, `PX/developer_ui.cpp:68`). So the proxy switches maps by setting dvars and letting the stock lobby load the map; it does not call a spawn or devmap function.
6. **Console interception.** `ConsoleText` (`PX/developer_ui.cpp:557-590`) on `Cbuf_AddText`: handles `mw_maps`, translates names (`commandtext::Translate`), and submits on the main thread (`Submit`, `PX/developer_ui.cpp:84-113`).
7. **Local player set-position.** `noclip` / `mw_setpos` (`PX/noclip.cpp:30-135`) on `G_CmdsMP_StringCommandDispatch`, using `SetOrigin` and `SetViewAngles` above.
8. **Fastfile load.** `customweapons::Install` hooks `DB_LoadFastfiles` (`PX/custom_weapons.cpp:649`) only when base-weapon packages are registered.
9. **Collision world gating.** `custom_physics.cpp:20-35` returns empty data only for type 23 and 29 from the two caller sites, and `custom_collision.cpp:395-410` hooks the instantiate and shutdown functions.
10. **World pointer.** `custom_render.cpp:136-146` reads `rgp_world` (`0x10C77870`) and the world name string at `world+0`. This is the only in-memory map-name source found.

## 12. Name conflicts (proxy name vs map public)

The map publics can alias identical-code-folded functions, so these need a semantic check before a live link depends on them.

| RVA | Proxy name (file) | Map public name (MAP line) | Impact |
|---|---|---|---|
| `0x15E1340` | `Overlay` (`replay_bindings.h:12`, hooked by `developer_ui.cpp:608`) | `ClGameModeApplicationMP::ClearWaitingForServer` (479791) | Overlay hook target may be a thunk (bytes `8B CA E9`). Verify before reuse |
| `0x11F1DD0` | `SetOrigin` (`replay_bindings.h:238-241`) | `G_ClientMP_SetPlayerStateFromNet` (388901) | The proxy comment ties it to the GSC `SetOrigin` at `0x1200590` (not verified). Name suggests a net-state setter |
| `0x11F1EE0` | `SetViewAngles` (`replay_bindings.h:244-249`) | `G_ActiveMP_ApplyClientViewAngles` (388906) | Same as above |
| `0xD871E0` | `ResetReader` | `DB_AsyncIWFileLoad_ReadBufferFinish` (282114) | Proxy behavior matches a reader reset; name differs |
| `0xD88120` | `InitInflator` | `DB_AsyncIWFileLoad_RegisterHashCallback` (282219) | Proxy calls it as inflator init. Name conflict |
| `0x12B36A0` | `FatalText` | `Com_SendSysErrorToDLog` | Name conflict |
| `0xCD24C0` | `MovementTrace` (`replay_bindings.h:304`) | `BgMissile_TraceMissileImpact` | Probably not a player movement trace. Name conflict |
| `0xCC5DB0` | `CheckLadderMove` (`replay_bindings.h:187`) | `PM_Normal_ResolveCollideMove` | Name conflict |
| `0x12279C0` | `DB_LoadZone` (`game.h:174`) | `GEntity_TraceCapsuleOnPlatform` (394197) | `DB_LoadZone` is UNKNOWN here |
| `0x16B2630` / `0x16B26A0` | `FS_FileExists` / `FS_FileExistsWithPath` (`game.h:185-186`) | `CG_EntityWorkers_SubmitTaskType5` (498852) / `CG_EntityWorkers_QueueTask_Int` | Not FS functions. Unhooked |
| `0x19CDC60` | `LUI_RegisterFunction` (`game.h:141`) | `LUI_ResetRenderState` (557422) | Unhooked |
| `0x1040E50` | `MatchRules_Frame` (`game.h:156`) | `Party_UpdateMatchmakerPlayers` (347392) | Unhooked |
| `0xD87E40` | `DB_AuthLoad_Inflate` (`replay_symbols.h`) | `DB_AsyncIWFileLoad_PumpDecodeQueue` (282206) | Name conflict |
| `0xD89E10` | `DB_BlockDecode` (`replay_symbols.h`) / `DB_AuthLoad_Inflate` (`game.h:177`) | `DB_CmpLoad_Decompress` (282407) | Name conflict |
| `0x3061A0` | `kGdiCloneBootstrap` (`game.h:38`, byte check in `startup_compat.cpp`) | `dynamic_initializer_for_AnticheatScanTable_Large_A` | Not a map entry point |

## 13. Gaps

1. No `DB_LoadXAssets`, `Com_LoadLevel`, `G_ParseSpawnVars`, `G_SpawnEntitiesFromString`, or `G_Spawn` in the map. `Scr_ParseSpawnVars2` and `G_Utils_SpawnEntity` are the only spawn-related names; their roles are not verified.
2. The `devmap` / `map` command registration site is not identified. `SV_DevMap_f` is a candidate only.
3. Asset ordinals are from the zonetool header. The binary tables (`k_XAssetTypeSizeTable` RVA `0x2458160`, `k_DB_XAssetTypeFuncs` RVA `0x23FD3A0`) were not read in this pass. Types 43 and 57, used by the proxy, are not in the enum. Type 63 (ScriptableMapEnts) is in the doc but not the enum.
4. No refdef or live camera matrix global. `rgp_world` is the only render-world global found.
5. No immediate-mode game debug line, box, or sphere draw API identified.
6. Dvar tokens are proven for 4 names. The rest of the 6727-entry table was not checked for this build beyond the rows above.
7. Asset-loader case-to-body bindings beyond types 8, 19, 24, 25, 31, 41, 52, 54, and 63 (from `LOADER_DISCOVERY.md`) are not verified for this build.
8. Name conflicts in section 12 are unresolved.
9. `DB_LoadZone`, `FS_FileExists`, and `MatchRules_Frame` addresses from `game.h` are not confirmed for this build. The `game.h` constants for `kSystemLinkSetMatchRules`, `kMatchRulesFrame`, `kLuiRegisterFunction`, `kGscrMapExists`, `kFsFileExists`, `kDbLoadZone`, `kLuaLoadfileFastfile`, `kDbAuthLoadInflateInternal`, and `kOmnvarRegisterFlagsFromStringTable` are declared but not hooked by any `.cpp` file.
10. Only one symbol source (the `.map`) was read for names. No PDB or IDA-verified signatures were used in this pass.

## 14. Key findings

- Image base `0x140000000`, build MD5 verified. Map RVA = section RVA + map offset, cross-checked against IDA.
- Map load is not hooked. The proxy switches maps by setting `ui_mapname` and `ui_gametype` through the console buffer in the stock lobby.
- Console entry is hookable at `Cbuf_AddText` (`0x15C1C20`), and the native buffer is `s_cbufMainBuffer` (`0xD216D70`).
- Map info injection (`Com_GameInfo_LoadMapInfoList` `0x10C7E70`, count `0xC4FD8B4`) and asset lookup (`DB_FindXAssetHeader` `0x11AA890`) are the two live map-data hooks.
- Several proxy names are wrong or unverified (section 12), most importantly `SetOrigin`/`SetViewAngles` and `Overlay`.

## Files consulted

- `E:\IW8\Binaries\1.20-replay\game_dx12_ship_replay.exe.map` (grep and streamed lookups only)
- `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe` (MD5 and first 4 KB PE header only)
- `E:\mw124\mw120rproxy\_ida_mw120_custom_map_flow.txt`, `_ida_mw120_seams.txt` (grep)
- `E:\mw124\mw120rproxy\iw8-zonetool\docs\LOADER_DISCOVERY.md` (grep)
- `E:\mw124\mw120rproxy\mw120rproxy\*` (game.h, replay_bindings.h, replay_symbols.h, dvar_names.h, custom_map_loader.cpp, custom_map_ui.cpp, custom_images.cpp, custom_physics.cpp, custom_collision.cpp, custom_weapons.cpp, custom_maps.cpp, developer_ui.cpp, noclip.cpp, dvar_patches.cpp, file_open_hook.cpp, engine_diagnostics.cpp, fastfile_diagnostics.cpp, dllmain.cpp, command_text.cpp, custom_render.cpp, mw120rproxy.ini)
- `E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\iw8\iw8_structs.h` (enum)
- `E:\odin-symbols\base.sqlite3`, `state.sqlite3` (`mode=ro`, cross-check only)
- `STOCK_MAP_RENDERING.md` and `USEFUL_TIPS.md` were grepped for asset and loader terms. Only `USEFUL_TIPS.md:157` matched (a generic failure message). Neither file was used for any address.
