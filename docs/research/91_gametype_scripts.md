# 91. How the game loads game type scripts, and what a map can carry

Date: 2026-11-12. Read-only study of the owner's recompiled game exe
`E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.exe` through its linker map (`tools\dw_disasm.py`, plus two scratch
helpers outside the project: a disassembler that does not stop at the map's `gl_*` labels, and a rel32 cross-reference
scanner). Follows research 87 section 5 and research 89 item 8.5. Nothing was run in the game. Nothing under
`E:\IW8\Builds` or `E:\DW\IW8` was written.

Labels: **VERIFIED** = read in the exe's code (function + address given) or in a named source file. **INFERRED** =
follows from verified facts but was not read directly.

Note on the tool: the map's `gl_XXXXXXXX` names are at shifted addresses and are not real function starts (for example
`DB_LinkXAssetEntry` is cut at `gl_1411AC849`; the function runs on to 0x1411D6CF9). Read past them, and compute
`lea` targets from the instruction instead of trusting a `gl_` note (the "Could not find script" string is at
0x1424E44D8, not at the `gl_1424784D8` the tool prints).

---

## 1. A level zone asset with the same name as a common zone asset

### 1.1 Zone flags (VERIFIED)

- Level zones: `DB_LoadLevelXAssets` (0x140F81750) calls `DB_DecideLevelLoad` (0x140F81570), which calls
  `DB_AddLevelLoadEntry` (0x140F81690) with `isBaseMap = 1`. The entry's flags are `(isBaseMap << 11) + 0x100`
  (0x140F816ED-0x140F816FA), so a map zone is loaded with flags **0x900**. (The voice zone added beside it gets 0x100,
  0x140F81610.) The DW harness uses the same value for `mp_donetsk` (`E:\DW\IW8\tools\bot_style_zone_harness.cpp:673`).
- Game mode zones: `Com_FastFile_InitModuleRecords` (0x1410D8E30) gives each load module a flag word at record + 0x20:
  2 (record 0x14C557080), 4 (0x14C557268), 1 (0x14C556CB0), 8 (0x14C557638), **0x10 (0x14C557820)**, 0x20 (0x14C557A08),
  0x100000 (0x14C557BF0), 0x40, 0x80. `Com_FastFile_RequestGameModeFastFiles` (0x1410DA010) puts, for MP:
  `global_stream_mp` / `global_mp` in record 0x14C557080 (flags 2), `global_core_mp` in 0x14C557268 (4),
  `common_stream_mp` / `common_mp` in 0x14C557638 (8), and **`common_base_mp` in 0x14C557820 (0x10)** (0x1410DA1BE-
  0x1410DA1DF; the third argument there is a size estimate, not flags). `Com_Fastfile_HandleModuleRequest` (0x1410D83E0)
  copies record + 0x20 into each XZoneInfo's flags word (0x1410D8499, 0x1410D8501), and `Com_Fastfile_UpdateModules`
  passes that list to `DB_LoadFastfiles` (0x1410DB2DE). The record's flag word is written nowhere else
  (cross-reference scan of 0x14C557840: only 0x1410D900A).
- So `common_base_mp` is flag group **0x10** and a map zone is **0x900**: different groups. `common_mp` is 0x8, which
  matches the DW client's own comment "common_mp group 0x8" (`DW_BotStyleZone.cpp`, operators block).

### 1.2 The duplicate rule in `DB_LinkXAssetEntry` (0x1411D6760, VERIFIED)

Asset entries are 20 bytes (`g_DBAssetEntryPool`): +0 header, +8 hash-chain next, +0xC override-chain next, +0x10 flags,
+0x11 type, +0x12 zone index. `DB_AssetEntryTable::FindAsset` (0x140F78010) returns the first entry on the hash chain
with that type and name: the head of the override chain.

When the incoming name already has an entry (0x1411D6854 onward), the function walks the override chain. For each
entry that has a zone index other than 0 and other than the zone being loaded, and whose zone flags (`ebx`) have no bit
of 0x3CFF000, it compares with the loading zone's flags (`eax`):

1. `ebx == eax` (same group): **discard the new asset** (0x1411D6897).
2. else if `eax` has bit 0x100000: discard when the loading zone's state byte is 0 and `ebx & 0x3F` (0x1411D689B-0x1411D68AF).
3. else if `eax & 0x200100` (a level zone has 0x100): discard when `ebx & 0x3F` (0x1411D68B1-0x1411D68BB).
4. else: discard when `eax & 0x3F` and `ebx & 1` (0x1411D68BD-0x1411D68C4).

Discard (0x1411D68FD): `DB_CheckZonePairConflicts` (bookkeeping only; its calls are zone-name / flag lookups, no
`Com_Error`), the new asset is released (`DB_ReleaseXAsset`, `DB_PatchMem_TryDiscard`), and the caller gets null; the
old entry stays the head.

If nothing discards, a new entry is created and `DB_AssetEntryTable_LinkEntry` (0x140F77EF0) **appends it to the tail of
the override chain**. The head is replaced (`DB_ReplaceHeadAssetEntry`, 0x1411DACC8) only when the old head has zone
index 0 (a placeholder or default entry, 0x1411D6CA7). So even an accepted duplicate does not change what a lookup
returns while the first zone stays loaded.

### 1.3 Answer (VERIFIED)

`scripts/mp/gametypes/war.gsc` in a map zone (0x900) meets the `common_base_mp` copy (0x10): rule 3 applies
(0x900 & 0x100, and 0x10 & 0x3F), so **the map zone's copy is discarded and every later lookup returns the stock
script**. The same holds for any asset a map zone shares by name with a zone of flags 0x01-0x20 (`global_*`,
`common_*`). Script loading uses the same lookup: `Scr_LoadScriptPrecacheList` -> `DW_BotStyle_ProcessScriptFile`
(0x1413592F0) -> `CO_FindAsset(0x34 = scriptfile, name)` (0x1411D4890), which returns the head entry's header
(asset type names at 0x1424C3A00: index 0x34 = `scriptfile`).

A name that exists in no earlier zone (for example `scripts/mp/gametypes/mymode.gsc`) has no entry, takes the normal add
path (0x1411D698D, `DB_AssetEntryTable::AddToTable`) and is found by later lookups.

The DW client's trick (`DW_BotStyleZone.cpp`, "Same-group duplicates are discarded by native DB_LinkXAssetEntry")
works because its zone is a copy of the `common_base_mp` record (same flags 0x10) placed before it in the same request:
rule 1 then discards the stock copy. A map zone cannot do this.

---

## 2. A game type name that is not in the game type table

### 2.1 Path from the lobby to the script (VERIFIED)

- `Party_GetGametype` (0x1411B3430): if the game state info is active (`s_gameStateInfoActiveState[0]`), the name comes
  from the match rules (`BG_MatchRulesData_GetGameType` -> `BG_MatchRulesData_GetRootString` on `ddl/mp/recipes.ddl`);
  otherwise from `ui_gametype`; if that is empty, `default_gametype_mp` (mode 2) or `default_gametype_cp` (mode 3).
- Private match start: `PartyHost_StartParty` copies `Party_GetGametype` into `ui_gametype` (0x1411C7B29);
  `PartyHost_SetupMatchForCurrentMap` (0x1411C81F0) only requires the map and game type strings to be non-empty, then
  calls `SV_CmdsMP_StartMapForPartyFlags` -> `SV_CmdsMP_StartMapWithSettings` (0x1413A76B0).
- `devmap`: `SV_DevMap_f` -> `Com_GameMode_SetDefaultGameTypeIfNeeded` (0x1410EF440) -> `Party_GetGametype` ->
  `SV_CmdsMP_StartMapWithSettings`.
- `SV_CmdsMP_StartMapWithSettings` copies the name, lower-cased, into the start settings (+0x40) with no lookup, then
  `SV_MainMP_ServerStart_Execute` (0x1413BB810). `map_restart` goes through `SV_CmdsMP_RequestMapRestart` /
  `SV_MainMP_ProcessPendingMapRestart` to the same function with the stored settings (call sites 0x1413A74EC,
  0x1413A70BC; the reuse of settings is INFERRED).
- `G_MainMP_InitGame` (0x14124DAF0) registers `scr_<name>_scorelimit` / `_roundlimit` from settings + 0x40
  (0x14124DCA1-0x14124DD28), calls `G_Main_SetupGameTypeQuick`, sets `g_gametype` to the name (0x14124DD7B), and
  `GScr_MainMP_LoadScripts` (0x14129ECB0) queues `scripts/mp/gametypes/<name>.gsc` with label 0x254 (`main`), required
  (`G_ScrMP_LoadGameTypeScript_Shared`, 0x140EADA10).

### 2.2 Checks found (VERIFIED)

- None of these functions looks the name up in `mp/gameTypesTable.csv`. The table lookups
  (`Com_GameInfo_GetGameTypeIndexForInternalName` 0x1410ED640, `Com_GameInfo_GetGameTypeForInternalName` 0x1410ED580)
  are called only from UI / LUI code and from the script function `GScr_MainMP_GetGameTypeIndex` (0x141294BD0, returns
  -1 for an unknown name). Cross-reference scan of both functions.
- `Com_GameMode_SetDefaultGameTypeIfNeeded` returns at once unless `AK_GameMode() == 3` (0x1410EF44E). In that mode it
  checks the name with `strstr` against the map's mapinfo game type list and, if absent, prints "Gametype %s not
  supported by map %s" and sets `ui_gametype` to the map's default game type. Mode 3 is the one whose default is
  `default_gametype_cp` (`Party_GetGametype`), so this is not the MP path.
- The only refusal: `GScr_MainMP_LoadScripts` calls `Scr_LoadScriptWithTempBuffer` for each queued script; a required
  script that is not found gives `Com_Error(1, "Could not find script '%s'")` (0x14129ED97-0x14129EDA8; string at
  0x1424E44D8), and a required label that is missing gives "Could not find label '%d' in script '%s'"
  (0x14129EDC2-0x14129EDD6).
- Unknown names fall back quietly elsewhere: `G_Main_SetupGameTypeQuick` (0x140FE3AE0) -> `BG_GetGameTypeQuickForName`
  (0x1411357F0) compares with `{"<NOT_SET>", "br", "arm"}` and returns 0 when not found; `MSG_GetNetFieldListsNameForGameType`
  (0x14130A040) uses the list `default` when the name has none (the table at 0x142504CA0 has `default` and `br` only).
- Match rules: `BG_MatchRulesData_GetGameTypeFromIndex` (0x1415DFBF0) errors if the recipe's game type field is not an
  enum type, so with a recipe active the game type can only be one of that enum's members (the members were not read).

### 2.3 Answer

An unknown name is neither refused nor replaced on the MP path: the engine builds `scripts/mp/gametypes/<name>.gsc` and
loads it (VERIFIED). If that script or its `main` is missing, the match fails to start with a drop error (VERIFIED).
The table only matters for what menus show and for script calls to `getgametypeindex` (VERIFIED). The limits are
outside the engine path: the DW lobby offers names from the map row (research 79; `DW_MapAvailability.cpp:750-784` for
Verdansk), `--gametype` in `DW_Devmap.cpp:261-263` checks only the characters (VERIFIED), and a private match running a
recipe takes the recipe's enum value instead of `ui_gametype` (VERIFIED in code; whether this client's private match
has a recipe active was not determined: `BG_GameStateInfo_SetUsingRecipe` 0x1415DEDE0 sets it).

---

## 3. Order at map start (VERIFIED)

Queued in `GScr_MainMP_LoadScripts_shared` (0x140EAD240), slots in `GameScriptData::ms_gScriptData`:

| Slot | Script | Label | Required |
|---|---|---|---|
| +0x74 | `scripts/mp/gametypes/<type>.gsc` | 0x254 `main` | yes (0x140EADAA3) |
| +0x00 | `scripts/mp/maps/<map>/<map>.gsc` | 0x254 `main` | no (0x140EAD3AD-0x140EAD3C8) |
| +0x78 | script id 14 | 0xF1 `codecallback_startgametype` | yes (0x140EADAE2) |
| +0x48 | `scripts/mp/bots/bots_gametype_<type>.gsc` | `main` | no |
| agents | `scripts/mp/agents/gametype_<type>.gsc` (`G_ScrMP_AddGametypeScriptRefs` 0x140EAD7D0) | `main` | no |

Label names from gsc-tool's IW8 token table (`E:\mw124\acts-perf\deps\gsc-tool\src\gsc\engine\iw8_token.cpp`:
0x0254 `main`, 0x00F1 `codecallback_startgametype`).

Run order in `G_MainMP_InitGame`, each through `Scr_ExecThreadWithoutCallback`:

1. 0x14124E370 `G_Spawn_LoadStructs`.
2. 0x14124E375 `GameScriptData_ExecSlot29Thread` (0x1412A02A0): slot +0x74, **the game type's `main`**.
3. 0x14124E37D: the `aitype/<name>.gsc` threads (`s_gameScriptDataPatchIds`).
4. 0x14124E390 `GameScriptData_RefreshPatchingProgress` (0x1412883E0): slot +0x00, **the map's `main`**, if it exists.
5. 0x14124E3B6 `GameScriptData_RefreshSlot30PatchingProgress` (0x1412A08D0): slot +0x78,
   **`codecallback_startgametype`**.
6. 0x14124E3F4 bots / agents game type mains (`Scr_AgentGameTypeMain`, slot +0x50).

(The map names these functions "PatchingProgress"; they are script runners: load the slot, run the thread, free it.)

So the map's `main` runs after the game type's `main` and before `codecallback_startgametype`. A thread runs until its
first `wait`, so everything a game type's `main` assigns before any wait is in place when the map's `main` starts.
Stock `war.gsc` (`[G120]` decompile, research 87) assigns `level.onstartgametype`, `level.getspawnpoint`,
`level.onnormaldeath`, `level.modeonspawnplayer` (and `level.onplayerkilled` under `scr_hotfoot`) in `main` with no
wait. **The map script's assignments therefore win** for such fields, and `level.onstartgametype` is called later
from the start-game-type callback, after the map script ran (INFERRED: the call site in `globallogic` is not in the
`[G120]` set). A field that the game type or `globallogic` sets after a wait or inside `callback_startgametype` would
overwrite the map's value; that has to be checked per field in the scripts (not done here).

---

## 4. Other things that decide whether a custom game type script in a map zone runs

- Zone before scripts (VERIFIED call order): `SV_MainMP_ServerStart_Execute` calls `SV_InitMP_SpawnServer_LoadMapAssets`
  (0x1413BB8DF, which calls `DB_LoadLevelXAssets` at 0x1413B8206) before `SvGameModeAppMP::ServerStart_Spawn`
  (0x1413BB953) -> `SV_GameMP_InitGameProgs` -> `SV_GameMP_InitGameVM` -> `G_MainMP_InitGame`. That the level zone's
  assets are fully linked by then is INFERRED (the world and spawn data from the same zone are needed by
  `G_MainMP_InitGame` too).
- The name must be new (section 1). A map zone cannot override a stock script of any name in `common_base_mp`, so a
  custom mode script must not reuse a stock file name, and any stock helper it needs changed must be copied under a
  new name.
- The game type script needs a `main` (label 0x254), or the match drops (section 2.2). Bots and agents scripts are
  optional; bots in a new game type have no `bots_gametype_<name>.gsc` unless the map zone carries one.
- Engine code keyed to the game type name: BR behaviour behind `gameTypeQuick` (`br`, `arm`) and the `br` net field
  list are not used for any other name (section 2.2). A BR-style mode under a new name would lose them.
- `scr_<name>_scorelimit` and `scr_<name>_roundlimit` are registered for any name (VERIFIED, 0x14124DCAA-0x14124DD28).
- Far calls from the new script into stock scripts (`scripts\mp\globallogic::init`, `scripts\mp\gameobjects::main`,
  ...): whether gsc-tool's id-0 + path form resolves at load is still open (research 87 section 5). A stock game type
  script calls dozens of these, so a new game type script depends on this more than a map script does.
- Menus and the scoreboard may look the name up in the table (`UI_GetGameTypeDisplayName`,
  `LUI_CoD_LuaCall_GetGameTypeIndexForInternalName`); what they show for an unknown name was not read.

---

## 5. Built on this (2026-11-12)

Option 1 below: **game mode rules** on top of a stock game type.

- Core `ModeRules` (game_modes.*): max health (1 .. 1000), move speed scale (0.2 .. 3), one weapon instead of the
  loadout, and a clip that refills every half second. Text form `health=50; speed=1.2; weapon=iw8_pi_golf21_mp;
  ammo=infinite` (worldspawn `iw8r_mode_rules`; preset JSON key `rules`).
- Map script (map_script.cpp): `main` starts `iw8r_mode_rules`, which waits for `level waittill("connected", player)`
  and gives each player a thread that waits for `spawned_player` (the stock notify, [G120] e.g. `br.gsc`), waits 0.05 s
  (after the game type's own spawn handling, `common.gsc:172` calls `level.modeonspawnplayer` on spawn), then sets
  `self.maxhealth` / `self.health`, `setmovespeedscale`, `takeallweapons` + `giveweapon(makeweapon(name))` +
  `switchtoweaponimmediate`, and threads the clip refill (`getcurrentweapon`, `weaponclipsize`, `setweaponammoclip`).
  Every builtin is in the converter's list (`iw8_gsc_builtins.inc`) and in no dropped list.
- Editor: Tools > Game Modes has a Rules line; Carry in this map writes `iw8r_mode_rules` (removed again by a preset
  without rules). Rules need the map to carry them (F5 alone sends only dvars).
- Fixed on the way: the build preflight treated `self <method>()` (and `level` / `game`) as a function call and flagged
  every builtin method called on `self`.
- Proof: core `game_mode_rules_text`, `game_mode_rules_preset_json`, `game_mode_rules_map_script`,
  `preflight_methods_on_self_and_level`; GUI check "a preset's rules are carried by the map"; a copy of the sample
  room with all four rules built through the real pipeline (`IW8Radiant.exe --compile`): preflight 0 errors, the
  converter translated the rules unchanged (functions renamed `mw120r_<map>_...` as usual) and gsc-tool compiled the
  map script (15,285 bytes). Not seen in game.

## What the editor can build (most certain first)

1. **Mode logic in the map script on top of a stock game type** (most certain). Start the match with a stock name
   (`war`, `dm`, `br`, ...), and in the generated `scripts/mp/maps/<map>/<map>.gsc` `main` reassign the `level.*`
   callbacks and set dvars. VERIFIED: the map `main` runs after the game type `main` and before
   `codecallback_startgametype`; the stock engine features (BR quick type, net fields, menus, playlists) all stay.
   Limits: the map script's calls into stock scripts are limited by the converter's whitelist and the open far-call
   question; fields that stock code sets after a wait must be checked one by one.
2. **A new game type script under a new name in the map zone** (engine side VERIFIED, runtime unproven). Carry
   `scripts/mp/gametypes/<new>.gsc` (with `main`) in the map zone and start with `ui_gametype <new>`: it links (new
   name), is not refused, and is loaded by name. Needed besides: converter support for a second script file; the name
   in the map row (`map.json` `gameTypes`) for the lobby to offer it, or a direct `ui_gametype` from the editor's F5;
   no recipe active; the far-call question answered. Loses BR engine behaviour unless the name is `br`.
3. **Replacing a stock game type script** (not possible from a map zone, VERIFIED): the map zone's copy is discarded.
   Only a zone loaded with the same flags before `common_base_mp` can do it, which needs a client change in
   `E:\DW\IW8` (the DW bot-style mechanism) and would apply to every map.
